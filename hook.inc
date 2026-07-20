#!/usr/bin/env bash
#
# hook script for qmail, dovecot, apache.
# It can handle SNI or skip mail sertver cert config.
# See hook.conf.template for more info
#
# More info
# here https://notes.sagredo.eu/en/qmail-notes-185/installing-a-let-s-encrypt-certificate-for-your-qmail-dovecot-and-apache-servers-233.html
# and here
# https://notes.sagredo.eu/en/qmail-notes-185/server-name-indication-sni-for-qmail-and-dovecot-331.html
#
# Roberto Puzzanghera [notes at sagredo dot eu - https://notes.sagredo.eu]
#

# ----------------------------------------------------------------------
# Logging
# ----------------------------------------------------------------------

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
RESET='\033[0m'
log_error()
{
  printf "${RED}ERROR:${RESET} %s\n" "$*" >&2
}
log_warn()
{
  printf "${YELLOW}WARNING:${RESET} %s\n" "$*" >&2
}
log_info()
{
  printf "${GREEN}INFO:${RESET} %s\n" "$*" >&2
}

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------

# Configuration for the script
CONF_FILE="$(dirname $0)/hook.conf"
if [ -r $CONF_FILE ]; then
  . $CONF_FILE
else
  log_error "Configuration file not found: $CONF_FILE"
  return 1 2>/dev/null || exit 1
fi

# Get vdominfo path
if [ "$MAKE_MAIL_CERTS" -eq 1 ]; then
  VDOMINFO=${VPOPMAILDIR}/bin/vdominfo
  if [ ! -x $VDOMINFO ]; then
    log_error "${VDOMINFO} not found"
    exit 1
  fi
fi


# ----------------------------------------------------------------------
# Domains
# ----------------------------------------------------------------------

# Read domains.txt and return each certificate entry with its type.
# The first domain is the default certificate, all following domains use SNI.
read_domains_txt()
{
  local DEFAULT=1

  while read -r CN SAN; do
    # skip empty lines
    [ -z "$CN" ] && continue
    # skip comments
    case "$CN" in
      \#*) continue ;;
    esac
    if [ "$DEFAULT" -eq 1 ]; then
      DEFAULT=0
      echo "DEFAULT $CN"
    else
      echo "SNI $CN"
    fi
  done < $DOMAINS_TXT
}

# Read domains.txt and return each certificate entry with all SANs.
# The first domain is the default certificate, all following domains use SNI.
read_domains_txt_full()
{
  local DEFAULT=1
  local CN
  local SAN

  while read -r CN SAN; do
    [ -z "$CN" ] && continue

    case "$CN" in
      \#*) continue ;;
    esac

    if [ "$DEFAULT" -eq 1 ]; then
      DEFAULT=0
      echo "DEFAULT $CN $SAN"
    else
      echo "SNI $CN $SAN"
    fi
  done < ${DOMAINS_TXT}
}

# Find the vpopmail domain matching a certificate CN.
# Domains and aliases are considered valid entries.
find_vpopmail_domain()
{
  local CN=$1
  local DOMAIN

  while read -r DOMAIN; do
    # skip empty lines
    [ -z "$DOMAIN" ] && continue
    case "$CN" in
      *.${DOMAIN})
        echo ${DOMAIN}
        return 0
        ;;
    esac
  done < <(${VDOMINFO} -n | sed 's/ .*//' | awk '{ print length, $0 }' | sort -rn | cut -d' ' -f2-)

  return 1
}


# ----------------------------------------------------------------------
# Certificate helpers
# ----------------------------------------------------------------------

# Get the default cert name by reading the 1st domain
# in 1st line of domain.txt
#
# smtp.example.com imap.example.com pop3.example.com
# smtp.example2.com imap.example2.com pop3.example2.com
# returns smtp.example.com
get_default_cert()
{
  local CN
  read -r CN _ < "$DOMAINS_TXT"
  echo "$CN"
}

# Check if the certificate files exist.
certificate_exists()
{
  local CN=$1
  local CERTDIR=${CERTSDIR}/${CN}

  [ -r ${CERTDIR}/fullchain.pem ] && [ -r ${CERTDIR}/privkey.pem ]
}


# ----------------------------------------------------------------------
# qmail
# ----------------------------------------------------------------------

# Install the default qmail certificate.
# The default certificate is the first entry in domains.txt.
install_qmail_default_cert()
{
  local CN=$1
  local CERTDIR=${CERTSDIR}/${CN}

  if ! certificate_exists $CN; then
    log_error "Certificate not found: ${CN}"
    return 1
  fi

  cat ${CERTDIR}/privkey.pem ${CERTDIR}/fullchain.pem > ${QMAILDIR}/control/servercert.pem
  chown ${CERT_UID}:${CERT_GID} ${QMAILDIR}/control/servercert.pem
  chmod 640 ${QMAILDIR}/control/servercert.pem
  log_info "Installed default qmail certificate for ${CN} in ${QMAILDIR}/control/servercert.pem"
}

# Install a qmail SNI certificate.
# The certificate name is the CN of the certificate entry.
install_qmail_sni_cert()
{
  local CN=$1
  local CERTDIR=${CERTSDIR}/${CN}
  local TARGETDIR=${QMAILDIR}/control/servercerts/${CN}

  if ! certificate_exists $CN; then
    log_error "Certificate not found: ${CN}"
    return 1
  fi

  mkdir -p ${TARGETDIR}
  cat ${CERTDIR}/privkey.pem ${CERTDIR}/fullchain.pem > ${TARGETDIR}/servercert.pem
  chown ${CERT_UID}:${CERT_GID} ${TARGETDIR}/servercert.pem
  chmod 640 ${TARGETDIR}/servercert.pem

  log_info "Installed qmail certificate for ${CN} in ${TARGETDIR}/servercert.pem"
}

# Synchronize a single qmail certificate.
# Called by dehydrated deploy_cert.
sync_qmail_cert()
{
  local CN=$1
  local DEFAULT

  DEFAULT=$(get_default_cert)

  if [ "$CN" = "$DEFAULT" ]; then
    install_qmail_default_cert $CN
  elif [ "$ENABLE_SNI" = "1" ]; then
    install_qmail_sni_cert $CN
  fi
}

# Synchronize qmail certificates from domains.txt.
sync_qmail_certificates()
{
  while read TYPE CN; do
    case "$TYPE" in
      DEFAULT)
        install_qmail_default_cert $CN
        ;;
      SNI)
        if [ "$ENABLE_SNI" = "1" ]; then
          install_qmail_sni_cert $CN
        fi
        ;;
    esac
  done < <(read_domains_txt)
}


# ----------------------------------------------------------------------
# Dovecot
# ----------------------------------------------------------------------

# Return success if the dovecot SNI block already exists.
dovecot_sni_block_exists()
{
  local DOMAIN=$1

  grep -Fq "local_name ${DOMAIN} {" ${DOVECOT_SNI_CONF}
}

# Add dovecot SNI blocks for a certificate.
# The certificate is identified by its dehydrated certificate name.
add_dovecot_sni_block()
{
  local CERTNAME=$1
  local DOMAIN=$(find_vpopmail_domain ${CERTNAME})

  if [ -z "$DOMAIN" ]; then
    log_error "Cannot find vpopmail domain for ${CERTNAME}"
    return 1
  fi

  cat >> ${DOVECOT_SNI_CONF} << EOF
local_name ${DOMAIN} {
  ssl_server_cert_file = ${CERTSDIR}/${CERTNAME}/fullchain.pem
  ssl_server_key_file = ${CERTSDIR}/${CERTNAME}/privkey.pem
}
local_name *.${DOMAIN} {
  ssl_server_cert_file = ${CERTSDIR}/${CERTNAME}/fullchain.pem
  ssl_server_key_file = ${CERTSDIR}/${CERTNAME}/privkey.pem
}

EOF

  log_info "Added dovecot SNI configuration for ${DOMAIN} in ${DOVECOT_SNI_CONF}"
}

# Synchronize a single dovecot SNI certificate.
# Called by dehydrated deploy_cert.
sync_dovecot_sni_cert()
{
  local CERTNAME=$1
  local DEFAULT
  local DOMAIN

  if [ "$ENABLE_SNI" != "1" ]; then
    return 0
  fi

  DEFAULT=$(get_default_cert)

  # The default certificate is managed by dovecot configuration.
  if [ "$CERTNAME" = "$DEFAULT" ]; then
    return 0
  fi

  if ! certificate_exists ${CERTNAME}; then
    log_error "Certificate not found: ${CERTNAME}"
    return 1
  fi

  DOMAIN=$(find_vpopmail_domain ${CERTNAME})

  if [ -z "$DOMAIN" ]; then
    log_error "Cannot find vpopmail domain for ${CERTNAME}"
    return 1
  fi

  if dovecot_sni_block_exists ${DOMAIN}; then
    return 0
  fi

  add_dovecot_sni_block ${CERTNAME}
}

# Rebuild dovecot SNI configuration from domains.txt.
# The first certificate entry is the default qmail certificate and is skipped.
rebuild_dovecot_sni_conf()
{
  local DEFAULT=1
  local CERTNAME

  if [ "$ENABLE_SNI" != "1" ]; then
    return 0
  fi

  mkdir -p $(dirname ${DOVECOT_SNI_CONF})
  # Create $DOVECOT_SNI_CONF before rebuilding it
  cat > ${DOVECOT_SNI_CONF} << EOF
# ----------------------------------------------------------------------
# This file is automatically generated by the dehydrated mail
# certificate management scripts.
#
# Do not edit this file manually.
#
# The configuration is rebuilt from ${DOMAINS_TXT} and the existing
# certificates under ${CERTSDIR}.
#
# Manual changes will be lost the next time mailcert-sync runs or a
# certificate is synchronized.
# ----------------------------------------------------------------------

EOF

  while read -r CERTNAME SAN; do
    # skip empty lines
    [ -z "$CERTNAME" ] && continue
    # skip comments
    case "$CERTNAME" in
      \#*) continue ;;
    esac

    # skip default certificate
    if [ "$DEFAULT" -eq 1 ]; then
      DEFAULT=0
      continue
    fi

    # skip missing certificates
    if ! certificate_exists ${CERTNAME}; then
      log_error "Certificate not found: ${CERTNAME}"
      continue
    fi

    add_dovecot_sni_block ${CERTNAME}
  done < ${DOMAINS_TXT}

  log_info "Rebuilt dovecot SNI configuration in ${DOVECOT_SNI_CONF}"
}


# ----------------------------------------------------------------------
# Apache
# ----------------------------------------------------------------------

# Rebuild Apache SNI configuration from domains.txt.
# The first certificate becomes the ServerName, all others become
# ServerAlias directives. Entries not matching a vpopmail domain
# are skipped.
rebuild_apache_sni_conf()
{
  local TYPE
  local CN
  local SAN
  local DOMAIN

  if [ "$ENABLE_SNI" != "1" ]; then
    return 0
  fi

  if [ "$ENABLE_APACHE_SNI_CONF" != "1" ]; then
    return 0
  fi

  mkdir -p $(dirname ${APACHE_SNI_CONF})

  cat > ${APACHE_SNI_CONF} << EOF
# ----------------------------------------------------------------------
# This file is automatically generated.
#
# Do not edit this file manually.
#
# The configuration is rebuilt from ${DOMAINS_TXT}.
# Manual changes will be lost.
# ----------------------------------------------------------------------

EOF

  {
    while read TYPE CN SAN; do
      if ! DOMAIN=$(find_vpopmail_domain ${CN}); then
        log_warn "Skipping Apache configuration for ${CN}: vpopmail domain not found"
        continue
      fi

      case "$TYPE" in
        DEFAULT)
          echo "ServerName ${CN}"

          if [ -n "$SAN" ]; then
            echo "ServerAlias ${SAN}"
          fi
          ;;

        SNI)
          echo "ServerAlias ${CN} ${SAN}"
          ;;
      esac

    done < <(read_domains_txt_full)
  } >> ${APACHE_SNI_CONF}

  log_info "Rebuilt Apache SNI configuration in ${APACHE_SNI_CONF}"
}


# ----------------------------------------------------------------------
# Synchronization
# ----------------------------------------------------------------------

# Synchronize qmail and dovecot certificates.
# This function is called by mailcert-sync.
mailcert_sync()
{
  if [ "$MAKE_MAIL_CERTS" != "1" ]; then
    log_info "Skipping mail certificates syncronization (MAKE_MAIL_CERTS=0)."
    return 0
  fi

  sync_qmail_certificates
  rebuild_dovecot_sni_conf
  rebuild_apache_sni_conf

  restart_servers
}


# ----------------------------------------------------------------------
# Servers
# ----------------------------------------------------------------------

# restart qmail
restart_qmail() {
  if [ ! -x $QMAILCTL ]; then
    log_error "$QMAILCTL not found"
    return 1
  fi
  $QMAILCTL restart
}

# restart dovecot
restart_dovecot() {
  if [ ! -x $DOVECOTCTL ]; then
    log_error "$DOVECOTCTL not found"
    return 1
  fi
  $DOVECOTCTL stop
  sleep 5
  $DOVECOTCTL start
}

# restart apache
restart_apache() {
  if [ ! -x $APACHECTL ]; then
    log_error "$APACHECTL not found"
    return 1
  fi
  log_info "Restarting apache"
  $APACHECTL stop
  $APACHECTL start
}


# restart the servers
restart_servers() {
  if [ "$MAKE_MAIL_CERTS" = "1" ]; then
    restart_qmail
    restart_dovecot
  fi
  restart_apache
}


# ----------------------------------------------------------------------
# Utility functions
# ----------------------------------------------------------------------

startup() {
  echo
  echo ==================================================
  echo
  date
  echo
}

# Send email alert to admin.
send_alert()
{
  local DOMAIN=$1
  local CERT_PATH=${CERTSDIR}/${DOMAIN}/cert.pem
  local EXPIRY=$(openssl x509 -noout -dates -in ${CERT_PATH} |grep notAfter |cut -d= -f2- |tr -s ' ' |cut -d' ' -f1,2,4)
  local DAYS_LEFT=$((($(date -d "$EXPIRY" +%s) - $(date -d "$(date +%F)" +%s))/86400))

  cat $MSG_TEMPLATE \
    | sed -e "s,@DOMAIN@,$DOMAIN,g" \
          -e "s,@FROM_EMAIL@,$FROM_EMAIL,g" \
          -e "s,@OWNER_EMAIL@,$OWNER_EMAIL,g" \
    | $QMAILDIR/bin/sendmail $OWNER_EMAIL
}
