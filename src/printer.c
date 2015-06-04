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

#include "printer.h"
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>





struct printer_conf_st {
	unsigned print;
	FILE *info;
	FILE *warning;
	FILE *error;
};

struct printer_conf_st printer_conf = {(PRINT_INFO | PRINT_WARNINGS | PRINT_ERRORS), NULL, NULL, NULL};

void print_setStream(unsigned print, FILE* stream) {
	if (print & PRINT_INFO) {
		printer_conf.info = stream;
	}
	if (print & PRINT_WARNINGS) {
		printer_conf.warning = stream;
	}
	if (print & PRINT_ERRORS) {
		printer_conf.error = stream;
	}
}

void print_enable(unsigned print) {
	printer_conf.print |= print;
}

void print_disable(unsigned print) {
	printer_conf.print &= ~print;
}

void print_init(void) {
	print_setStream(PRINT_RESULT | PRINT_INFO | PRINT_WARNINGS, stdout);
	print_setStream(PRINT_ERRORS, stderr);
	print_enable(PRINT_RESULT | PRINT_INFO | PRINT_WARNINGS | PRINT_ERRORS);
}


void print_result(const char *format, ... ) {
	if (printer_conf.print & PRINT_RESULT) {
		va_list va;
		va_start(va, format);
		vfprintf(stdout, format, va);
		va_end(va);
	}
}

void print_info(const char *format, ... ) {
	if (printer_conf.print & PRINT_INFO) {
		va_list va;
		va_start(va, format);
		vfprintf(stdout, format, va);
		va_end(va);
	}
}

void print_warnings(const char *format, ... ) {
	if (printer_conf.print & PRINT_WARNINGS) {
		va_list va;
		va_start(va, format);
		vfprintf(stdout, format, va);
		va_end(va);
	}
}

void print_errors(const char *format, ... ) {
	if (printer_conf.print & PRINT_ERRORS) {
		va_list va;
		va_start(va, format);
		vfprintf(stderr, format, va);
		va_end(va);
	}
}



