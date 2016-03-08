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

#include <ksi/ksi.h>
#include <ksi/pkitruststore.h>
#include <string.h>
#include "param_set/param_set.h"
#include "tool_box/ksi_init.h"
#include "tool_box/smart_file.h"
#include "ksitool_err.h"
#include "printer.h"
#include "api_wrapper.h"

#ifdef _WIN32
#	include <io.h>
#	include <fcntl.h>
#include <stdlib.h>
#else
#	include <limits.h>
#	include <sys/time.h>
#endif

static int tool_init_ksi_logger(KSI_CTX *ksi, ERR_TRCKR *err, PARAM_SET *set, SMART_FILE **log) {
	int res;
	SMART_FILE *tmp = NULL;
	char *outLogfile = NULL;

	if (ksi == NULL || err == NULL || set == NULL || log == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	PARAM_SET_getStr(set, "log", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &outLogfile);

	/* Set logging. */
	if (outLogfile != NULL) {
		res = SMART_FILE_open(outLogfile, "w", &tmp);
		if (res != KT_OK) {
			ERR_TRCKR_ADD(err, res, "Error:%s.", errToString(res));
			goto cleanup;
		}

		res = KSI_CTX_setLoggerCallback(ksi, KSITOOL_LOG_SmartFile, tmp);
		ERR_CATCH_MSG(err, res, "Error: Unable to set logger callback function.");

		res = KSI_CTX_setLogLevel(ksi, KSI_LOG_DEBUG);
		ERR_CATCH_MSG(err, res, "Error: Unable to set logger log level.");
	}

	res = KT_OK;
	*log = tmp;
	tmp = NULL;

cleanup:

	SMART_FILE_close(tmp);

	return res;
}

static int tool_init_ksi_network_provider(KSI_CTX *ksi, ERR_TRCKR *err, PARAM_SET *set) {
	int res;
	char *aggr_url = NULL;
	char *aggr_user = NULL;
	char *aggr_pass = NULL;
	char *ext_url = NULL;
	char *ext_user = NULL;
	char *ext_pass = NULL;
	char *pub_url = NULL;
	int networkConnectionTimeout = -1;
	int networkTransferTimeout = -1;


	if (ksi == NULL || err == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Extract values from the set.
     */
	PARAM_SET_getStr(set, "S", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &aggr_url);
	PARAM_SET_getStr(set, "X", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &ext_url);
	PARAM_SET_getStr(set, "P", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &pub_url);

	PARAM_SET_getStr(set, "aggr-user", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &aggr_user);
	PARAM_SET_getStr(set, "aggr-key", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &aggr_pass);
	PARAM_SET_getStr(set, "ext-user", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &ext_user);
	PARAM_SET_getStr(set, "ext-key", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &ext_pass);

	PARAM_SET_getObj(set, "C", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, (void**)&networkConnectionTimeout);
	PARAM_SET_getObj(set, "c", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, (void**)&networkTransferTimeout);

	aggr_user = aggr_user == NULL ? "anon" : aggr_user;
	aggr_pass = aggr_pass == NULL ? "anon" : aggr_pass;
	ext_user = ext_user == NULL ? "anon" : ext_user;
	ext_pass = ext_pass == NULL ? "anon" : ext_pass;


	/**
	 * Set service urls.
     */
	if (ext_url != NULL) {
		res = KSI_CTX_setExtender(ksi, ext_url, ext_user, ext_pass);
		ERR_CATCH_MSG(err, res, "Error: Unable set extender.");
	}

	if (aggr_url != NULL) {
		res = KSI_CTX_setAggregator(ksi, aggr_url, aggr_user, aggr_pass);
		ERR_CATCH_MSG(err, res, "Error: Unable set aggregator.");
	}

	if (pub_url != NULL) {
		res = KSI_CTX_setPublicationUrl(ksi, pub_url);
		ERR_CATCH_MSG(err, res, "Error: Unable set publication URL.");
	}

	/**
	 * Set service timeouts.
     */
	if (networkTransferTimeout > 0) {
		res = KSI_CTX_setConnectionTimeoutSeconds(ksi, networkConnectionTimeout);
		ERR_CATCH_MSG(err, res, "Error: Unable set connection timeout.");
	}

	if (networkTransferTimeout > 0) {
		res = KSI_CTX_setTransferTimeoutSeconds(ksi, networkTransferTimeout);
		ERR_CATCH_MSG(err, res, "Error: Unable set transfer timeout.");
	}

	res = KT_OK;

cleanup:

	return res;
}

static int tool_init_ksi_pub_cert_constraints(KSI_CTX *ksi, ERR_TRCKR *err, PARAM_SET *set) {
	int res;
	int i = 0;
	char *constraint = NULL;
	int constraint_count = 0;
	KSI_CertConstraint *constraintArray = NULL;

	if (ksi == NULL || err == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Extract all constraints from the highest priority level.
	 */
	res = PARAM_SET_getValueCount(set, "{cnstr}", NULL, PST_PRIORITY_HIGHEST, &constraint_count);
	if (res != PST_OK && res != PST_PARAMETER_EMPTY) {
		ERR_TRCKR_ADD(err, res, NULL);
		goto cleanup;
	}

	if (constraint_count == 0) {
		res = KT_OK;
		goto cleanup;
	}

	/**
	 * Generate array of publications file signatures certificate constraints and
	 * load it with values.
     */
	constraintArray = KSI_malloc(sizeof(KSI_CertConstraint) * (1 + constraint_count));
	if (constraintArray == NULL) {
		ERR_TRCKR_ADD(err, res = KT_OUT_OF_MEMORY, NULL);
		goto cleanup;
	}

	for (i = 0; i < constraint_count + 1; i++) {
		constraintArray[i].oid = NULL;
		constraintArray[i].val = NULL;
	}

	for (i = 0; i < constraint_count; i++) {
		char *oid = NULL;
		char *value = NULL;
		char tmp[1024];
			PARAM_SET_getStr(set, "cnstr", NULL, PST_PRIORITY_NONE, i, &constraint);
			strncpy(tmp, constraint, sizeof(tmp));

			oid = tmp;
			value = strchr(tmp, '=');
			if (value == NULL) {
				ERR_TRCKR_ADD(err, res = KT_INVALID_CMD_PARAM, "Error: Unable to parse constraint.");
				goto cleanup;
			}

			*value = '\0';
			value++;

		constraintArray[i].oid = KSI_malloc(strlen(oid) + 1);
		constraintArray[i].val = KSI_malloc(strlen(value) + 1);
		if (constraintArray[i].oid == NULL || constraintArray[i].val == NULL) {
			ERR_TRCKR_ADD(err, res = KSI_OUT_OF_MEMORY, NULL);
			goto cleanup;
		}

		strcpy(constraintArray[i].oid, oid);
		strcpy(constraintArray[i].val, value);
	}

	/**
	 * Configure KSI publications file constraints.
     */
	res = KSI_CTX_setDefaultPubFileCertConstraints(ksi, constraintArray);
	ERR_CATCH_MSG(err, res, "Error: Unable to add cert constraints.");

	res = KT_OK;

cleanup:

	if (constraintArray)  {
		for (i = 0; i < constraint_count; i++) {
			KSI_free(constraintArray[i].oid);
			KSI_free(constraintArray[i].val);
		}

		KSI_free(constraintArray);
	}

	return res;
}

static int tool_init_ksi_trust_store(KSI_CTX *ksi, ERR_TRCKR *err, PARAM_SET *set) {
	int res;
	KSI_PKITruststore *refTrustStore = NULL;
	int i = 0;
	int V, W;
	char *lookupFile = NULL;
	char *lookupDir = NULL;

	if (ksi == NULL || err == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Check if there are trust store related files or directories.
     */
	V = PARAM_SET_isSetByName(set,"V");
	W = PARAM_SET_isSetByName(set,"W");

	/**
	 * Configure KSI trust store.
	 * TODO: look over Windows and Linux compatibility related with trust store
	 * configuration.
	 * TODO: Fix trust store to ignore other certificates if there are certificates
	 * specified by the user (TOOL-20).
     */
	if (V || W) {
		res = KSI_CTX_getPKITruststore(ksi, &refTrustStore);
		ERR_CATCH_MSG(err, res, "Error: Unable to get PKI trust store.");

		i = 0;
		while(PARAM_SET_getStr(set, "V", NULL, PST_PRIORITY_HIGHEST, i++, &lookupFile) == PST_OK) {
			res = KSI_PKITruststore_addLookupFile(refTrustStore, lookupFile);
			ERR_CATCH_MSG(err, res, "Error: Unable to add cert to PKI trust store.");
		}

		i = 0;
		while(PARAM_SET_getStr(set, "W", NULL, PST_PRIORITY_HIGHEST, i++, &lookupDir) == PST_OK) {
			res = KSI_PKITruststore_addLookupDir(refTrustStore, lookupDir);
			ERR_CATCH_MSG(err, res, "Error: Unable to add lookup dir to PKI trust store.");
		}
	}

	res = KT_OK;

cleanup:

	return res;
}

static int tool_init_ksi_publications_file(KSI_CTX *ksi, ERR_TRCKR *err, PARAM_SET *set) {
	int res = KSI_UNKNOWN_ERROR;
	KSI_PublicationsFile *tmpPubFile = NULL;
	char *inPubFileName = NULL;

	if (ksi == NULL || err == NULL || set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

//	TODO: Implement workaround to get publications file from file.
//	res = KSI_CTX_setPublicationsFile(ksi, tmpPubFile);
//	ERR_CATCH_MSG(err, res, "Error: Unable to configure publications file.");
//	tmpPubFile = NULL;

	res = KT_OK;

cleanup:

	KSI_PublicationsFile_free(tmpPubFile);

	return res;
}

int TOOL_init_ksi(PARAM_SET *set, KSI_CTX **ksi, ERR_TRCKR **error, SMART_FILE **ksi_log) {
	int res;
	ERR_TRCKR *err = NULL;
	KSI_CTX *tmp = NULL;
	SMART_FILE *tmp_log = NULL;

	if (set == NULL || ksi == NULL || error == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Initialize error tracker and configure output parameter immediately to be
	 * able to track errors if this function fails.
     */
	err = ERR_TRCKR_new(print_errors);
	if (err == NULL) {
		res = KT_OUT_OF_MEMORY;
		print_errors("Error: Unable to initialize error tracker.");
		goto cleanup;
	}

	*error = err;

	/**
	 * Initialize KSI_CTX and configure:
	 * 1) Logger.
	 * 2) Network provider (service info).
	 * 3) TODO:pubfile.
	 * 4) Publications file constraints.
	 * 5) Trust store.
     */
	res = KSI_CTX_new(&tmp);
	if (res != KSI_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to initialize KSI context.");
		goto cleanup;
	}

	res = tool_init_ksi_logger(tmp, err, set, &tmp_log);
	if (res != KT_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to configure KSI logger.");
		goto cleanup;
	}

	res = tool_init_ksi_network_provider(tmp, err, set);
	if (res != KT_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to configure network provider.");
		goto cleanup;
	}

	res = tool_init_ksi_publications_file(tmp, err, set);
	if (res != KT_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to configure KSI publications file.");
		goto cleanup;
	}

	res = tool_init_ksi_pub_cert_constraints(tmp, err, set);
	if (res != KT_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to configure KSI publications file constraints.");
		goto cleanup;
	}

	res = tool_init_ksi_trust_store(tmp, err, set);
	if (res != KT_OK) {
		ERR_TRCKR_ADD(err, res, "Error: Unable to configure KSI trust store.");
		goto cleanup;
	}

	*ksi = tmp;
	*ksi_log = tmp_log;
	tmp = NULL;
	tmp_log = NULL;
	res = KT_OK;


cleanup:

	KSI_CTX_free(tmp);
	SMART_FILE_close(tmp_log);

	return res;
}
