/**************************************************************************
 *
 * GUARDTIME CONFIDENTIAL
 *
 * Copyright (C) [2015] Guardtime, Inc
 * All Rights Reserved
 *
 * NOTICE:  All information contained herein is, and remains, the
 * property of Guardtime Inc and its suppliers, if any.
 * The intellectual and technical concepts contained herein are
 * proprietary to Guardtime Inc and its suppliers and may be
 * covered by U.S. and Foreign Patents and patents in process,
 * and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this
 * material is strictly forbidden unless prior written permission
 * is obtained from Guardtime Inc.
 * "Guardtime" and "KSI" are trademarks or registered trademarks of
 * Guardtime Inc.
 */

#include "conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ksi/compatibility.h>
#include "param_set/param_set.h"
#include "ksitool_err.h"
#include "tool_box/tool_box.h"
#include "tool_box/param_control.h"
#include "tool_box/smart_file.h"
#include "printer.h"

char* CONF_generate_param_set_desc(char *description, const char *flags, char *buf, size_t buf_len) {
	char *extra_desc = NULL;
	int is_S = 0;
	int is_X = 0;
	int is_P = 0;
	size_t count = 0;


	if (buf == NULL || buf_len == 0) return NULL;

	is_S = strchr(flags, 'S') != NULL ? 1 : 0;
	is_X = strchr(flags, 'X') != NULL ? 1 : 0;
	is_P = strchr(flags, 'P') != NULL ? 1 : 0;

	extra_desc = (description == NULL) ? "" : description;
	count += KSI_snprintf(buf + count, buf_len - count, "{C}{c}%s", extra_desc);

	if (is_S) {
		count += KSI_snprintf(buf + count, buf_len - count, "{S}{aggr-user}{aggr-key}");
	}

	if (is_X) {
		count += KSI_snprintf(buf + count, buf_len - count, "{X}{ext-user}{ext-key}");
	}

	if (is_P) {
		count += KSI_snprintf(buf + count, buf_len - count, "{P}{cnstr}{V}{W}{publications-file-no-verify}");
	}

	return buf;
}

int CONF_createSet(PARAM_SET **conf) {
	int res;
	PARAM_SET *tmp = NULL;
	char buf[1024];

	if (conf == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	res = PARAM_SET_new(CONF_generate_param_set_desc(NULL, "SXP", buf, sizeof(buf)), &tmp);
	if (res != PST_OK) goto cleanup;

	res = CONF_initialize_set_functions(tmp, "SXP");
	if (res != PST_OK) goto cleanup;

	*conf = tmp;
	tmp = NULL;
	res = PST_OK;

cleanup:

	PARAM_SET_free(tmp);

	return res;
}

int CONF_initialize_set_functions(PARAM_SET *conf, const char *flags) {
	int res = KT_UNKNOWN_ERROR;
	int is_S = 0;
	int is_X = 0;
	int is_P = 0;


	if (conf == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	is_S = strchr(flags, 'S') != NULL ? 1 : 0;
	is_X = strchr(flags, 'X') != NULL ? 1 : 0;
	is_P = strchr(flags, 'P') != NULL ? 1 : 0;

	if (is_P) {
		res = PARAM_SET_addControl(conf, "{V}{W}", isFormatOk_inputFile, isContentOk_inputFileRestrictPipe, convertRepair_path, NULL);
		if (res != PST_OK) goto cleanup;

		res = PARAM_SET_addControl(conf, "{P}", isFormatOk_url, NULL, convertRepair_url, NULL);
		if (res != PST_OK) goto cleanup;

		res = PARAM_SET_addControl(conf, "{publications-file-no-verify}", isFormatOk_flag, NULL, NULL, NULL);
		if (res != PST_OK) goto cleanup;

		res = PARAM_SET_addControl(conf, "{cnstr}", isFormatOk_constraint, NULL, convertRepair_constraint, NULL);
		if (res != PST_OK) goto cleanup;
	}

	if (is_S) {
		res = PARAM_SET_addControl(conf, "{S}", isFormatOk_url, NULL, convertRepair_url, NULL);
		if (res != PST_OK) goto cleanup;

		res = PARAM_SET_addControl(conf, "{aggr-user}{aggr-key}", isFormatOk_userPass, NULL, NULL, NULL);
		if (res != PST_OK) goto cleanup;
	}

	if (is_X) {
		res = PARAM_SET_addControl(conf, "{X}", isFormatOk_url, NULL, convertRepair_url, NULL);
		if (res != PST_OK) goto cleanup;

		res = PARAM_SET_addControl(conf, "{ext-key}{ext-user}", isFormatOk_userPass, NULL, NULL, NULL);
		if (res != PST_OK) goto cleanup;
	}

	res = PARAM_SET_addControl(conf, "{c}{C}", isFormatOk_int, isContentOk_int, NULL, extract_int);
	if (res != PST_OK) goto cleanup;


	res = KT_OK;

cleanup:

	return res;
}

static int conf_fromFile(PARAM_SET *set, const char *fname, const char *source, int priority) {
	int res;

	if (fname == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	if (!SMART_FILE_doFileExist(fname)) {
		res = KT_IO_ERROR;
		goto cleanup;
	}

	if (!SMART_FILE_isReadAccess(fname)) {
		res = KT_NO_PRIVILEGES;
		goto cleanup;
	}

	res = PARAM_SET_readFromFile(set, fname, source, priority);
	if (res != KT_OK) goto cleanup;


cleanup:

	return res;
}

int CONF_fromEnvironment(PARAM_SET *set, const char *env_name, char **envp, int priority, char *buf, size_t len) {
	int res;
	char name[1024];
	char value[2024];


	if (env_name == NULL || envp == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	while (*envp!=NULL) {
		if (STRING_extractAbstract(*envp, NULL, "=", name, sizeof(name), NULL, NULL, NULL) == NULL) {
			envp++;
			continue;
		}

		if (strcmp(name, env_name) == 0) {
			if (STRING_extract(*envp, "=", NULL, value, sizeof(value)) == NULL) {
				res = KT_INVALID_INPUT_FORMAT;
				goto cleanup;
			}

			if (buf) KSI_strncpy(buf, value, len);

			res = conf_fromFile(set, value, env_name, priority);
			if (res != PST_OK) goto cleanup;

			break;
		}

        envp++;
    }


	res = KT_OK;

cleanup:

	return res;
}

int CONF_isInvalid(PARAM_SET *set) {
	if (set == NULL) return 1;

	if (!PARAM_SET_isFormatOK(set) || PARAM_SET_isUnknown(set) || PARAM_SET_isTypoFailure(set)
			|| PARAM_SET_isSetByName(set, "publications-file-no-verify")
			|| PARAM_SET_isSyntaxError(set)) {
		return 1;
	} else {
		return 0;
	}
}

int conf_report_errors(PARAM_SET *set, const char *fname, int res) {
	char buf[0xffff];
	if (res == KT_IO_ERROR) {
		print_errors("Error: Configurations file '%s' pointed by KSI_CONF does not exist.\n", fname);
		res = KT_INVALID_CONF;
		goto cleanup;
	} else if (res == KT_NO_PRIVILEGES) {
		print_errors("Error: User has no privileges to access configurations file '%s' pointed by KSI_CONF.\n", fname);
		res = KT_INVALID_CONF;
		goto cleanup;
	} else if (CONF_isInvalid(set)) {
		print_errors("Error: Configurations file '%s' pointed by KSI_CONF is invalid:\n", fname);
		print_errors("%s\n", CONF_errorsToString(set, "  ", buf, sizeof(buf)));
		res = KT_INVALID_CONF;
		goto cleanup;
	}
cleanup:
	return res;
}

char *CONF_errorsToString(PARAM_SET *set, const char *prefix, char *buf, size_t buf_len) {
	char tmp[4096];
	size_t count = 0;

	if (set == NULL || buf == NULL || buf_len == 0) return NULL;

	if (PARAM_SET_isSyntaxError(set)) {
		PARAM_SET_syntaxErrorsToString(set, prefix, tmp, sizeof(tmp));
		count += KSI_snprintf(buf + count, buf_len - count, "%s", tmp);
		goto cleanup;
	}

	if (PARAM_SET_isTypoFailure(set)) {
			PARAM_SET_typosToString(set, PST_TOSTR_DOUBLE_HYPHEN, prefix, tmp, sizeof(tmp));
			count += KSI_snprintf(buf + count, buf_len - count, "%s", tmp);
	}

	if (PARAM_SET_isUnknown(set)) {
			PARAM_SET_unknownsToString(set, prefix, tmp, sizeof(tmp));
			count += KSI_snprintf(buf + count, buf_len - count, "%s", tmp);
	}

	if (!PARAM_SET_isFormatOK(set)) {
		PARAM_SET_invalidParametersToString(set, prefix, getParameterErrorString, tmp, sizeof(tmp));
		count += KSI_snprintf(buf + count, buf_len - count, "%s", tmp);
	}

	if (PARAM_SET_isSetByName(set, "publications-file-no-verify")) {
		count += KSI_snprintf(buf + count, buf_len - count, "%sConfigurations flag 'publications-file-no-verify' can only be defined on command-line.\n",
				prefix == NULL ? "" : prefix);

	}

cleanup:
	return buf;
}


int conf_run(int argc, char** argv, char **envp) {
	char buf[0xffff];
	argc;
	argv;
	envp;

	print_result("%s\n", conf_help_toString(buf, sizeof(buf)));

	return EXIT_SUCCESS;
}

char *conf_help_toString(char *buf, size_t len) {
	size_t count = 0;

	if (buf == NULL || len == 0) return NULL;

	count += KSI_snprintf(buf + count, len - count,
		"KSI Configurations file help:\n\n"
		"To define default KSI service access parameters a configurations file must be\n"
		"defined. Default configurations file is searched from the path pointed from\n"
		"environment variable KSI_CONF. Values from the default configurations file can\n"
		"be overloaded with a specific configurations file given from the command-line\n"
		"(--conf <file>) or just typing the same parameters to the command-line.\n"
		"\n"
		"Configurations file is composed with following parameters described below.\n"
		"Following key-value pairs must be placed one per line. To write a comment use #\n"
		"at the beginning of the line. If unknown or invalid key-value pairs are used,\n"
		"an error is generated until user applies all fixes needed.\n\n"
		"If some parameter values contain whitespace characters double quote mark\"\n"
		"must be used to wrap the entire value. If double quote mark have to be used\n"
		"inside the value part an escape character must be typed before the quote mark\n"
		"like that \\\"\n"
		"\n"
		"All known parameters that can be used:\n"
		);

	count += KSI_snprintf(buf + count, len - count,
		" -S <URL>  - specify signing service URL.\n"
		" --aggr-user <str>\n"
		"           - user name for signing service.\n"
		" --aggr-key <str>\n"
		"           - HMAC key for signing service.\n"
		" -X <URL>  - specify extending service URL.\n"
		" --ext-user <str>\n"
		"           - user name for extending service.\n"
		" --ext-key <str>\n"
		"           - HMAC key for extending service.\n"
		" -P <URL>  - specify publications file URL (or file with URI scheme 'file://').\n"
		" --cnstr <oid=value>\n"
		"           - publications file certificate verification constraints.\n"
		" -V <file> - specify an OpenSSL-style trust store file for publications file verification.\n"
		" -W <dir>  - specify an OpenSSL-style trust store directory for publications file verification.\n"
		" -c <num>  - set network transfer timeout, after successful connect, in s.\n"
		" -C <num>  - set network Connect timeout in s (is not supported with TCP client).\n"
		" --publications-file-no-verify\n"
		"           - a flag to force the tool to trust the publications file without\n"
		"             verifying it. The flag can only be defined on command-line to avoid\n"
		"             the usage of insecure configurations files. It must be noted that the\n"
		"             option is insecure and may only be used for testing.\n"
		"\n"
		"\n"
		);

	count += KSI_snprintf(buf + count, len - count,
		"An example configurations file:\n\n"
		" # --- BEGINNING ---\n"
		" # KSI Signing service parameters:\n"
		" -S http://ksigw.test.guardtime.com:3333/gt-signingservice\n"
		" --aggr-user anon\n"
		" --aggr-key anon\n"
		"\n"
		" # KSI Extending service:\n"
		" # Note that ext-key real value is &h/J\"kv\\G##\n"
		" -X http://ksigw.test.guardtime.com:8010/gt-extendingservice\n"
		" --ext-user anon\n"
		" --ext-key \"&h/J\\\"kv\\\\G##\"\n"
		"\n"
		" # KSI Publications file:\n"
		" -P http://verify.guardtime.com/ksi-publications.bin\n"
		" --cnstr email=publications@guardtime.com\n"
		" --cnstr \"org=Guardtime AS\"\n"
		" # --- END ---\n"
		"\n"
		);


	return buf;
}

const char *conf_get_desc(void) {
	return "KSI Service configuration file utility.";
}