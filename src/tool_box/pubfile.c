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

#include <stdio.h>
#include <stdlib.h>
#include <ksi/ksi.h>
#include <ksi/net.h>
#include <ksi/hashchain.h>
#include <ksi/compatibility.h>
#include "param_set/param_set.h"
#include "param_set/task_def.h"
#include "tool_box/ksi_init.h"
#include "tool_box/tool_box.h"
#include "tool_box/param_control.h"
#include "tool_box/task_initializer.h"
#include "tool_box/smart_file.h"
#include "api_wrapper.h"
#include "printer.h"
#include "debug_print.h"
#include "obj_printer.h"
#include "conf.h"
#include "tool.h"

static int pubfile_download(PARAM_SET *set, ERR_TRCKR *err, KSI_CTX *ksi, KSI_PublicationsFile **pubfile);
static int pubfile_create_pub_string(PARAM_SET *set, ERR_TRCKR *err, KSI_CTX *ksi, COMPOSITE *extra);
static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set);

int pubfile_run(int argc, char** argv, char **envp) {
	int res;
	TASK *task = NULL;
	TASK_SET *task_set = NULL;
	PARAM_SET *set = NULL;
	KSI_PublicationsFile *pubfile = NULL;
	KSI_CTX *ksi = NULL;
	SMART_FILE *logfile = NULL;
	ERR_TRCKR *err = NULL;
	COMPOSITE extra;
	char buf[2048];
	int d;

	/**
	 * Extract command line parameters.
     */
	res = PARAM_SET_new(
			CONF_generate_desc("{o}{d}{v}{T}{conf}{log}", buf, sizeof(buf)),
			&set);
	if (res != KT_OK) goto cleanup;

	res = TASK_SET_new(&task_set);
	if (res != PST_OK) goto cleanup;

	res = generate_tasks_set(set, task_set);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_getServiceInfo(set, argc, argv, envp);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_check_analyze_report(set, task_set, 0.5, 0.1, &task);
	if (res != KT_OK) goto cleanup;

	res = TOOL_init_ksi(set, &ksi, &err, &logfile);
	if (res != KT_OK) goto cleanup;

	d = PARAM_SET_isSetByName(set, "d");

	extra.ctx = ksi;
	extra.err = err;

	switch(TASK_getID(task)) {
		case 0:
		case 1:
		case 2:
		case 3:
			res = pubfile_download(set, err, ksi, &pubfile);
		case 4:
			res = pubfile_create_pub_string(set, err, ksi, &extra);
		break;
		default:
			res = KT_UNKNOWN_ERROR;
			goto cleanup;
		break;
	}

	if (TASK_getID(task) == 4) goto cleanup;

	if (d) {
		OBJPRINT_publicationsFileDump(pubfile);
	}


cleanup:
	print_progressResult(res);
	KSITOOL_KSI_ERRTrace_save(ksi);

	if (res != KT_OK) {
		KSI_LOG_debug(ksi, "\n%s", KSITOOL_KSI_ERRTrace_get());
		print_info("\n");
		DEBUG_verifyPubfile(ksi, set, res, pubfile);

		print_info("\n");
		if (d) ERR_TRCKR_printExtendedErrors(err);
		else  ERR_TRCKR_printErrors(err);
	}

	SMART_FILE_close(logfile);
	PARAM_SET_free(set);
	TASK_SET_free(task_set);
	ERR_TRCKR_free(err);
	KSI_CTX_free(ksi);

	return errToExitCode(res);
}
char *pubfile_help_toString(char*buf, size_t len) {
	size_t count = 0;

	count += KSI_snprintf(buf + count, len - count,
		"Usage:\n"
		"%s pubfile -P <url> [--cnstr <oid=value>]... [-V <file>]...\n"
		"        [-W <file>]... [-o <pubfile.bin>] [-d] [-v] [more options]\n"
		"%s pubfile -T <time> -X <url> [--ext-user <user> --ext-key <pass>]\n\n"

		" -P <url>  - specify publications file URL (or file with uri scheme 'file://').\n"
		" --cnstr <oid=value>\n"
		"           - publications file certificate verification constraints.\n"
		" -o <file> - output file name to store publications file.\n"
		" -v        - perform publications file verification."
		" -X <url>  - specify extending service URL.\n"
		" --ext-user <str>\n"
		"           - user name for extending service.\n"
		" --ext-key <str>\n"
		"           - HMAC key for extending service.\n"
		" -T <time> - specify time to create a publication string for as the number of seconds\n"
		"             since 1970-01-01 00:00:00 UTC or time string formatted as \"YYYY-MM-DD hh:mm:ss\".\n"
		" -d        - print detailed information about processes and errors.\n"
		" --conf <file>\n"
		"           - specify a configurations file to override default service\n"
		"             information. It must be noted that service info from\n"
		"             command-line will override the configurations file.\n"
		" --log <file>\n"
		"           - Write libksi log into file.",
		TOOL_getName(),
		TOOL_getName()
	);

	return buf;
}

const char *pubfile_get_desc(void) {
	return "KSI general publications file tool.";
}

static int pubfile_download(PARAM_SET *set, ERR_TRCKR *err, KSI_CTX *ksi, KSI_PublicationsFile **pubfile) {
	int res;
	int d;
	int v;
	char *save_to = NULL;
	KSI_PublicationsFile *tmp = NULL;
	KSI_Integer *pubTime = NULL;
	KSI_PublicationRecord *pubRec = NULL;
	KSI_PublicationData *pubData = NULL;
	char buf[1024];

	if (set == NULL || ksi == NULL || err == NULL || pubfile == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	d = PARAM_SET_isSetByName(set, "d");
	v = PARAM_SET_isSetByName(set, "v");

	res = PARAM_SET_getObj(set, "o", NULL, PST_PRIORITY_NONE, PST_INDEX_LAST, (void**)&save_to);
	if (res != PST_OK && res != PST_PARAMETER_EMPTY) goto cleanup;

	print_progressDesc(d, "Downloading publications file... ");
	res = KSITOOL_receivePublicationsFile(err, ksi, &tmp);
	ERR_CATCH_MSG(err, res, "Error: Unable to get publication file.");
	print_progressResult(res);

	print_progressDesc(d, "Extracting latest publication time... ");
	res = KSI_PublicationsFile_getLatestPublication(tmp, NULL, &pubRec);
	ERR_CATCH_MSG(err, res, "Error: Unable to extract publication record.");
	res = KSI_PublicationRecord_getPublishedData(pubRec, &pubData);
	ERR_CATCH_MSG(err, res, "Error: Unable to extract publication data.");
	res = KSI_PublicationData_getTime(pubData, &pubTime);
	ERR_CATCH_MSG(err, res, "Error: Unable to extract publication time.");
	print_progressResult(res);

	print_info("Latest publication (%i) %s+00:00.\n",
			KSI_Integer_getUInt64(pubTime),
			KSI_Integer_toDateString(pubTime, buf, sizeof(buf)));

	if (v) {
		print_progressDesc(d, "Verifying publications file... ");
		res = KSITOOL_verifyPublicationsFile(err, ksi, tmp);
		ERR_CATCH_MSG(err, res, "Error: Unable to verify publication file.");
		print_progressResult(res);
	}

	if (save_to != NULL) {
		print_progressDesc(d, "Saving publications file... ");
		res = KSI_OBJ_savePublicationsFile(err, ksi, tmp, save_to);
		ERR_CATCH_MSG(err, res, "Error: Unable to save publications file.");
		print_progressResult(res);
	}

	res = KT_OK;
	*pubfile = tmp;
	tmp = NULL;

cleanup:
	print_progressResult(res);

	return res;
}

static int pubfile_create_pub_string(PARAM_SET *set, ERR_TRCKR *err, KSI_CTX *ksi, COMPOSITE *extra) {
	int res;
	int d;
	KSI_Integer *start = NULL;
	KSI_Integer *end = NULL;
	KSI_Integer *reqID = NULL;
	KSI_ExtendReq *extReq = NULL;
	KSI_RequestHandle *request = NULL;
	KSI_ExtendResp *extResp = NULL;
	KSI_Integer *respStatus = NULL;
	KSI_CalendarHashChain *chain = NULL;
	KSI_DataHash *extHsh = NULL;
	KSI_PublicationData *tmpPubData = NULL;
	KSI_Integer *pubTime = NULL;
	char buf[1024];


	if (set == NULL || ksi == NULL || err == NULL || extra  == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}


	d = PARAM_SET_isSetByName(set, "t");
//	PARAM_SET_getIntValue(set, "T", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &start);
	res = PARAM_SET_getObjExtended(set, "T", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, extra, (void**)&start);
	if (res != KT_OK) goto cleanup;
	end = KSI_Integer_ref(start);

	print_progressDesc(d, "Sending extend request... ");

	res = KSI_Integer_new(ksi, (KSI_uint64_t)start, &reqID);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_ExtendReq_new(ksi, &extReq);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_ExtendReq_setAggregationTime(extReq, start);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	start = NULL;
	res = KSI_ExtendReq_setPublicationTime(extReq, end);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	end = NULL;

	res = KSI_ExtendReq_setRequestId(extReq, reqID);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_sendExtendRequest(ksi, extReq, &request);
	ERR_CATCH_MSG(err, res, "Error: Unable to send extend request.");
	res = KSI_RequestHandle_perform(request);
	ERR_CATCH_MSG(err, res, "Error: Unable to send extend request.");

	res = KSITOOL_RequestHandle_getExtendResponse(err, ksi, request, &extResp);
	ERR_APPEND_KSI_ERR(err, res, KSI_NETWORK_ERROR);
	ERR_CATCH_MSG(err, res, "Error: Unable to get extend response.");
	res = KSI_ExtendResp_getStatus(extResp, &respStatus);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));

	if (respStatus == NULL || !KSI_Integer_equalsUInt(respStatus, 0)) {
		KSI_Utf8String *errm = NULL;
		res = KSI_ExtendResp_getErrorMsg(extResp, &errm);
		if (res == KSI_OK && KSI_Utf8String_cstr(errm) != NULL) {
			ERR_TRCKR_ADD(err, res, "Extender returned error %llu: '%s'.", (unsigned long long)KSI_Integer_getUInt64(respStatus), KSI_Utf8String_cstr(errm));
		}else{
			ERR_TRCKR_ADD(err, res, "Extender returned error %llu.", (unsigned long long)KSI_Integer_getUInt64(respStatus));
		}
	}

	print_progressResult(res);


	print_progressDesc(1, "Getting publication string... ");

	res = KSI_ExtendResp_getCalendarHashChain(extResp, &chain);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_CalendarHashChain_aggregate(chain, &extHsh);
	ERR_CATCH_MSG(err, res, "Error: Unable to aggregate calendar hash chain.");
	res = KSI_CalendarHashChain_getPublicationTime(chain, &pubTime);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_PublicationData_new(ksi, &tmpPubData);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_PublicationData_setImprint(tmpPubData, extHsh);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_PublicationData_setTime(tmpPubData, pubTime);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));
	res = KSI_CalendarHashChain_setPublicationTime(chain, NULL);
	ERR_CATCH_MSG(err, res, "Error: %s", errToString(res));

	print_progressResult(res);
	print_info("\n");

	print_result("%s\n", KSITOOL_PublicationData_toString(tmpPubData, buf,sizeof(buf)));


cleanup:
	print_progressResult(res);

	KSI_Integer_free(start);
	KSI_Integer_free(end);
	KSI_ExtendReq_free(extReq);
	KSI_ExtendResp_free(extResp);
	KSI_RequestHandle_free(request);
	KSI_PublicationData_free(tmpPubData);

	return res;
}

static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set) {
	int res;

	if (set == NULL || task_set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Configure parameter set, control, repair and object extractor function.
     */
	res = CONF_initialize_set_functions(set);
	if (res != KT_OK) goto cleanup;

	PARAM_SET_addControl(set, "{conf}", isFormatOk_inputFile, isContentOk_inputFile, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{o}{log}", isFormatOk_path, NULL, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{T}", isFormatOk_utcTime, isContentOk_utcTime, NULL, extract_utcTime);
	PARAM_SET_addControl(set, "{d}{v}", isFormatOk_flag, NULL, NULL, NULL);

	/**
	 * Define possible tasks.
     */
	/*					  ID	DESC										MAN			ATL		FORBIDDEN	IGN	*/
	TASK_SET_add(task_set, 0,	"Verify publications file.",				"P,v",		NULL,	"T,d,o",	NULL);
	TASK_SET_add(task_set, 1,	"Dump publications file.",					"P,d",		NULL,	"T,o",		NULL);
	TASK_SET_add(task_set, 2,	"Save publications.",						"P,o",		NULL,	"T,d",		NULL);
	TASK_SET_add(task_set, 3,	"Save and dump publications file.",			"P,o,d",	NULL,	"T",		NULL);
	TASK_SET_add(task_set, 4,	"Create publication string.",				"T,X",		NULL,	NULL,		NULL);

cleanup:

	return res;
}