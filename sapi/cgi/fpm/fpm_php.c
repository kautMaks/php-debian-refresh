
	/* $Id: fpm_php.c,v 1.22.2.4 2008/12/13 03:21:18 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpm_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "php.h"
#include "php_main.h"
#include "php_ini.h"
#include "ext/standard/dl.h"

#include "fastcgi.h"

#include "fpm.h"
#include "fpm_php.h"
#include "fpm_cleanup.h"
#include "fpm_worker_pool.h"

static int zend_ini_alter_master(char *name, int name_length, char *new_value, int new_value_length, int stage TSRMLS_DC)
{
	zend_ini_entry *ini_entry;
	char *duplicate;

	if (zend_hash_find(EG(ini_directives), name, name_length, (void **) &ini_entry) == FAILURE) {
		return FAILURE;
	}

	duplicate = strdup(new_value);

	if (!ini_entry->on_modify
		|| ini_entry->on_modify(ini_entry, duplicate, new_value_length,
			ini_entry->mh_arg1, ini_entry->mh_arg2, ini_entry->mh_arg3, stage TSRMLS_CC) == SUCCESS) {
		ini_entry->value = duplicate;
		ini_entry->value_length = new_value_length;
	} else {
		free(duplicate);
	}

	return SUCCESS;
}

static void fpm_php_disable(char *value, int (*zend_disable)(char *, uint TSRMLS_DC) TSRMLS_DC)
{
	char *s = 0, *e = value;

	while (*e) {
		switch (*e) {
			case ' ':
			case ',':
				if (s) {
					*e = '\0';
					zend_disable(s, e - s TSRMLS_CC);
					s = 0;
				}
				break;
			default:
				if (!s) {
					s = e;
				}
				break;
		}
		e++;
	}

	if (s) {
		zend_disable(s, e - s TSRMLS_CC);
	}
}

static int fpm_php_apply_defines(struct fpm_worker_pool_s *wp)
{
	TSRMLS_FETCH();
	struct key_value_s *kv;

	for (kv = wp->config->php_defines; kv; kv = kv->next) {
		char *name = kv->key;
		char *value = kv->value;
		int name_len = strlen(name);
		int value_len = strlen(value);

		if (!strcmp(name, "extension") && *value) {
			zval zv;

#if defined(PHP_VERSION_ID) && (PHP_VERSION_ID >= 50300)
			php_dl(value, MODULE_PERSISTENT, &zv, 1 TSRMLS_CC);
#else
			zval filename;
			ZVAL_STRINGL(&filename, value, value_len, 0);
#if (PHP_MAJOR_VERSION >= 5)
			php_dl(&filename, MODULE_PERSISTENT, &zv, 1 TSRMLS_CC);
#else
			php_dl(&filename, MODULE_PERSISTENT, &zv TSRMLS_CC);
#endif
#endif
			continue;
		}

		zend_ini_alter_master(name, name_len + 1, value, value_len, PHP_INI_STAGE_ACTIVATE TSRMLS_CC);

		if (!strcmp(name, "disable_functions") && *value) {
			char *v = strdup(value);
#if (PHP_MAJOR_VERSION >= 5)
			PG(disable_functions) = v;
#endif
			fpm_php_disable(v, zend_disable_function TSRMLS_CC);
		}
		else if (!strcmp(name, "disable_classes") && *value) {
			char *v = strdup(value);
#if (PHP_MAJOR_VERSION >= 5)
			PG(disable_classes) = v;
#endif
			fpm_php_disable(v, zend_disable_class TSRMLS_CC);
		}
	}

	return 0;
}

static int fpm_php_set_allowed_clients(struct fpm_worker_pool_s *wp)
{
	if (wp->listen_address_domain == FPM_AF_INET) {
		fcgi_set_allowed_clients(wp->config->allowed_clients);
	}

	return 0;
}

static int fpm_php_set_fcgi_mgmt_vars(struct fpm_worker_pool_s *wp)
{
	char max_workers[10 + 1]; /* 4294967295 */
	int len;

	len = sprintf(max_workers, "%u", (unsigned int) wp->config->pm->max_children);

	fcgi_set_mgmt_var("FCGI_MAX_CONNS", sizeof("FCGI_MAX_CONNS")-1, max_workers, len);
	fcgi_set_mgmt_var("FCGI_MAX_REQS",  sizeof("FCGI_MAX_REQS")-1,  max_workers, len);

	return 0;
}

char *fpm_php_script_filename(TSRMLS_D)
{
	return SG(request_info).path_translated;
}

char *fpm_php_request_method(TSRMLS_D)
{
	return (char *) SG(request_info).request_method;
}

size_t fpm_php_content_length(TSRMLS_D)
{
	return SG(request_info).content_length;
}

static void fpm_php_cleanup(int which, void *arg)
{
	TSRMLS_FETCH();
	php_module_shutdown(TSRMLS_C);
	sapi_shutdown();
}

void fpm_php_soft_quit()
{
	fcgi_set_in_shutdown(1);
}

int fpm_php_init_main()
{
	if (0 > fpm_cleanup_add(FPM_CLEANUP_PARENT, fpm_php_cleanup, 0)) {
		return -1;
	}

	return 0;
}

int fpm_php_init_child(struct fpm_worker_pool_s *wp)
{
	if (0 > fpm_php_apply_defines(wp) ||
		0 > fpm_php_set_allowed_clients(wp) ||
		0 > fpm_php_set_fcgi_mgmt_vars(wp)) {
		return -1;
	}

	return 0;
}
