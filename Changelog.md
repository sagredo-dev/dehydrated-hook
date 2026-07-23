# Changelog

- Jul 23, 2026 (v. 4.0)
  - Added a new dehydrated-renew wrapper script to execute certificate renewal and perform
    service synchronization for SNI only when one or more certificates have actually changed.
  - Introduced a change flag mechanism (dehydrated.changed) to avoid unnecessary service
    reloads and restarts if any certificate has been actually deployed.
  - Consolidated post-renew operations into the new cert_sync() function, providing a single
    entry point for synchronizing qmail, Dovecot, and Apache certificate configuration after
    successful renewals. The same cert_sync() can be called via stand-alone script, which
    doesn't involve the dehydrated run.
  - Reduced unnecessary service interruptions by performing synchronization once per renewal
    run instead of once per renewed certificate.
  - qmail certificate is built only if MAKE_MAIL_CERTS=1 is set. If MAKE_MAIL_CERTS=0 the hook
    script only deploys the certificates (use for webserver).
  - Server Server Name Indication (SNI) stuff for qmail and dovecot can be disabled by setting
    ENABLE_SNI=0 (default).
  - ServerName and ServerAlias entries for Apache and SNI domains can be optionally set with
    ENABLE_APACHE_SNI_CONF=1.

- Feb 25, 2026 (v. 3.0)
  - the hook.sh script optionally configures qmail and dovecot for Server Name Indication (SNI).

- Jun 6, 2025 (v 2.0)
  - dehydrated now launches a hook.sh script which handles the post-installation tasks (assemble
    and copy the certificate into the qmail dir, restart the server and eventually alert the
    administrator in case of problems). It replaces the old scripts.

- Feb 22, 2025
  - Let’s Encrypt have announced that they will end their free alerting service. Added a script
    to achieve the same internally.

- Aug 6, 2023
  - The certificates installation is now based on dehydrated. The previous documentation based
    on certbot will be left as is at the bottom of this page, but it won't be updated anymore.

- May 18, 2023
  - added the option --key-type rsa to the certbot command, to avoid that certbot will silently
    default to ECDSA the private key format, which results not understandable by my openssl-1.1.
    In this way the format of the private key will be RSA. More info [here](https://community.letsencrypt.org/t/getting-a-rsa-privkey-from-the-letsencrypt-generated-pem/188797).

