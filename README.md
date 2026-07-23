# Hook script for `dehydrated`

Customized hook script for `qmail`/`dovecot`/`apache` based servers using [`dehydrated`](https://github.com/dehydrated-io/dehydrated).

## How it works

- Download the package in _/etc/dehydrated_ and create a suitable symbolic link:

  ```
  cd /etc/dehydrated
  git clone https://github.com/sagredo-dev/dehydrated-hook.git
  ln -s dehydrated-hook scripts
  cd scripts  
  ```
  
- Create your config file:

  ```cp -p hook.conf.template hook.conf```

- If not interested in hnadling `qmail`/`dovecot` certs, just set `MAKE_MAIL_CERTS=0`.
- If you set `MAKE_MAIL_CERTS=1` and `ENABLE_SNI=1`, then Server Name Indication (SNI) for `qmail`, `dovecot` and eventually `apache` (`ENABLE_APACHE_SNI_CONF`) are handled.
- Setup a cronjob. The `dehydrated-renew` is used to reload the services only when all certs are deployed: 

  ```51 2 6 * * root /etc/dehydrated/scripts/dehydrated-renew -c -g >> /var/log/dehydrated 2>&1```

## More info and support

- [Installing a Let's Encrypt certificate for your qmail, dovecot and apache servers](https://www.sagredo.eu/en/qmail-notes-185/installing-a-let-s-encrypt-certificate-for-your-qmail-dovecot-and-apache-servers-233.html).
- [Server Name Indication (SNI) for qmail and dovecot](https://notes.sagredo.eu/en/qmail-notes-185/server-name-indication-sni-for-qmail-and-dovecot-331.html)
