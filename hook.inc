#!/usr/bin/env bash

# Configuration for the script:
CONF_FILE="$(dirname $0)/$(basename $0 .sh).conf"
[ -f ${CONF_FILE} ] && . ${CONF_FILE}

send_alert()
{
  # Send email alert to admin $1 is DOMAIN

  CERT_PATH=/etc/dehydrated/certs/$1/cert.pem
  EXPIRY=$(openssl x509 -noout -dates -in ${CERT_PATH} |grep notAfter |cut -d= -f2- |tr -s ' ' |cut -d' ' -f1,2,4)
  DAYS_LEFT=$((($(date -d "$EXPIRY" +%s) - $(date -d "$(date +%F)" +%s))/86400))

  cat $MSG_TEMPLATE \
    | sed -e "s,@DOMAIN@,$1,g" \
          -e "s,@FROM_EMAIL@,$FROM_EMAIL,g" \
          -e "s,@OWNER_EMAIL@,$OWNER_EMAIL,g" \
    | sendmail $OWNER_EMAIL
}

# Add SNI block to dovecot conf. It exit safely if it already exists.
# File modified is defined in DOVECOT_SNI_CONF
add_sni_block() {
  CERT=$1

  # touch $DOVECOT_SNI_CONF if it does not exist already
  if [ ! -e $DOVECOT_SNI_CONF ]; then
    touch $DOVECOT_SNI_CONF
  fi

  # Calculate the domain, so that mx.domain.tld will get domain.tld
  DOMAIN=$(echo ${CERT} | sed 's/^\([^\.]*\)\.\([^\.]*\)\./\2\./')

  # Check if the local_name block already exists in $DOVECOT_SNI_CONF
  if ! grep -qE "^local_name[[:space:]]+${DOMAIN}[[:space:]]*\{" "${DOVECOT_SNI_CONF}"; then

    # Append $DOMAIN configuration block to $DOVECOT_SNI_CONF
    cat >> "$DOVECOT_SNI_CONF" <<- EOF

	local_name ${DOMAIN} {
	  ssl_server_cert_file = ${CERTSDIR}/${CERT}/fullchain.pem
	  ssl_server_key_file  = ${CERTSDIR}/${CERT}/privkey.pem
	}
EOF
  echo "Added ${DOMAIN} block to ${DOVECOT_SNI_CONF}"
  else
    echo "${DOMAIN} block already exists in ${DOVECOT_SNI_CONF}"
  fi

  # Check if the local_name block already exists in $DOVECOT_SNI_CONF
  if ! grep -qE "^local_name[[:space:]]+\*\.${DOMAIN}[[:space:]]*\{" "${DOVECOT_SNI_CONF}"; then

    # Append *.$DOMAIN configuration block to $DOVECOT_SNI_CONF
    cat >> "$DOVECOT_SNI_CONF" <<- EOF
	local_name *.${DOMAIN} {
	  ssl_server_cert_file = ${CERTSDIR}/${CERT}/fullchain.pem
	  ssl_server_key_file  = ${CERTSDIR}/${CERT}/privkey.pem
	}
EOF
  echo "Added *.${DOMAIN} block to ${DOVECOT_SNI_CONF}"
  else
    echo "*.${DOMAIN} block already exists in ${DOVECOT_SNI_CONF}"
  fi
}

make_qmail_certs() {
  # backup/assemble/copy the qmail certificate
  # you can avoid the call to this function in hook.conf if this is not needed

  # skip if we don't want to build certs for qmail (this is not a mail server)
  if [ "$MAKE_MAIL_CERTS" != "1" ]; then
    exit 0
  fi

  DOMAIN=$1

  # qmail cert backup
  if [ ! -d "${QMAILDIR}/control/certs_backup" ]; then
    mkdir -p ${QMAILDIR}/control/certs_backup
  fi

  # install the certificate in control/servercert.pem
  # if $DOMAIN is the default cert name.
  if [ "$DOMAIN" = "$DEFAULT_CERTNAME" ]; then
    echo "Setting up the default cert for qmail: $DEFAULT_CERTNAME -> ${QMAILDIR}/control/servercert.pem"
    CERTDIR=${CERTSDIR}/${DEFAULT_CERTNAME}
    cp -p ${QMAILDIR}/control/*.pem ${QMAILDIR}/control/certs_backup/
    cat ${CERTDIR}/privkey.pem ${CERTDIR}/fullchain.pem > ${QMAILDIR}/control/servercert.pem
    chown vpopmail:vchkpw ${QMAILDIR}/control/*.pem
    chmod o-r ${QMAILDIR}/control/*.pem
  elif [ "$ENABLE_SNI" == "1" ]; then
  # install the other non default certs in control/servercerts/<FQDN>/servercert.pem
    # create servercerts dir
    if [ ! -d ${QMAILDIR}/control/servercerts ]; then
      mkdir -p ${QMAILDIR}/control/servercerts
      chown -R vpopmail:vchkpw ${QMAILDIR}/control/servercerts
      chmod 750 ${QMAILDIR}/control/servercerts
    fi

    # get the cert to create
    CERT=""
    for ELEMENT in "${SNI_CERTS[@]}"; do
      if [ "${DOMAIN}" == "${ELEMENT}" ]; then
        CERT=${ELEMENT}
      fi
    done

    if [ -n "${CERT}" ]; then
      # $CERT is the domain claiming for a cert
      echo "Setting up the cert for qmail: $CERT -> ${QMAILDIR}/control/servercerts/${CERT}"

      # create the control/servertcerts/<FQDN> dir
      if [ ! -d ${QMAILDIR}/control/servercerts/${CERT} ]; then
        mkdir -p ${QMAILDIR}/control/servercerts/${CERT}
        chown -R vpopmail:vchkpw ${QMAILDIR}/control/servercerts/${CERT}
        chmod -R o-r ${QMAILDIR}/control/servercerts/${CERT}
      else
        # backup if it already exists
        cp -rp ${QMAILDIR}/control/servercerts/${CERT} ${QMAILDIR}/control/certs_backup/
      fi

      # assemble the cert for qmail
      CERTDIR=${CERTSDIR}/${CERT}
      cat ${CERTDIR}/privkey.pem ${CERTDIR}/fullchain.pem > ${QMAILDIR}/control/servercerts/${CERT}/servercert.pem
      chown vpopmail:vchkpw ${QMAILDIR}/control/servercerts/${CERT}/*.pem
      chmod o-r ${QMAILDIR}/control/servercerts/${CERT}/*.pem

      # setup SNI in dovecot for the domain $DOMAIN
      add_sni_block ${CERT}
    fi
  fi
  restart_qmail
  restart_dovecot
}

restart_qmail() {
  # restart qmail
  $QMAILCTL restart
}

restart_dovecot() {
  # restart dovecot
  $DOVECOTCTL stop
  sleep 5
  $DOVECOTCTL start
}

restart_apache() {
  # restart apache
  echo "Restarting apache"
  $APACHECTL stop
  $APACHECTL start
}

startup() {
  echo
  echo ==================================================
  echo
  date
  echo
}
