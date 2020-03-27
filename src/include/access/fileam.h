/*-------------------------------------------------------------------------
*
* fileam.h
*	  external file access method definitions.
*
* Portions Copyright (c) 2007-2008, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/include/access/fileam.h
*
*-------------------------------------------------------------------------
*/
#ifndef FILEAM_H
#define FILEAM_H

#include "access/formatter.h"
#include "access/relscan.h"
#include "access/sdir.h"
#include "access/url.h"
#include "utils/rel.h"

/*
 * ExternalInsertDescData is used for storing state related
 * to inserting data into a writable external table.
 */
typedef struct ExternalInsertDescData
{
	Relation	ext_rel;
	URL_FILE   *ext_file;
	char	   *ext_uri;		/* "command:<cmd>" or "tablespace:<path>" */
	bool		ext_noop;		/* no op. this segdb needs to do nothing (e.g.
								 * mirror seg) */

	TupleDesc	ext_tupDesc;
	Datum	   *ext_values;
	bool	   *ext_nulls;

	FmgrInfo   *ext_custom_formatter_func; /* function to convert to custom format */
	List	   *ext_custom_formatter_params; /* list of defelems that hold user's format parameters */

	FormatterData *ext_formatter_data;

	struct CopyStateData *ext_pstate;	/* data parser control chars and state */

} ExternalInsertDescData;

typedef ExternalInsertDescData *ExternalInsertDesc;

/*
 * ExternalSelectDescData is used for storing state related
 * to selecting data from an external table.
 */
typedef struct ExternalSelectDescData
{
	ProjectionInfo *projInfo;   /* Information for column projection */
	List *filter_quals;         /* Information for filter pushdown */

} ExternalSelectDescData;

typedef enum DataLineStatus
{
	LINE_OK,
	LINE_ERROR,
	NEED_MORE_DATA,
	END_MARKER
} DataLineStatus;

extern FileScanDesc external_beginscan(Relation relation,
				   uint32 scancounter, List *uriList,
				   char *fmtOptString, char fmtType, bool isMasterOnly,
				   int rejLimit, bool rejLimitInRows,
				   char logErrors, int encoding, List *extOptions);
extern void external_rescan(FileScanDesc scan);
extern void external_endscan(FileScanDesc scan);
extern void external_stopscan(FileScanDesc scan);
extern ExternalSelectDesc
external_getnext_init(PlanState *state);
extern HeapTuple
external_getnext(FileScanDesc scan,
                 ScanDirection direction,
                 ExternalSelectDesc desc);
extern ExternalInsertDesc external_insert_init(Relation rel);
extern Oid	external_insert(ExternalInsertDesc extInsertDesc, HeapTuple instup);
extern void external_insert_finish(ExternalInsertDesc extInsertDesc);
extern void external_set_env_vars(extvar_t *extvar, char *uri, bool csv, char *escape, char *quote, bool header, uint32 scancounter);
extern char *linenumber_atoi(char *buffer, size_t bufsz, int64 linenumber);

/* prototypes for functions in url_execute.c */
extern int popen_with_stderr(int *rwepipe, const char *exe, bool forwrite);
extern int pclose_with_stderr(int pid, int *rwepipe, StringInfo sinfo);
extern char *make_command(const char *cmd, extvar_t *ev);

extern List *parseCopyFormatString(Relation rel, char *fmtstr, char fmttype);
extern List *appendCopyEncodingOption(List *copyFmtOpts, int encoding);

/*
 * A data error happened. This code block will always be inside a PG_CATCH()
 * block right when a higher stack level produced an error. We handle the error
 * by checking which error mode is set (SREH or all-or-nothing) and do the right
 * thing accordingly. Note that we MUST have this code in a macro (as opposed
 * to a function) as elog_dismiss() has to be inlined with PG_CATCH in order to
 * access local error state variables.
 */
#define CHECK_DATA_EXCEPTION \
/* SREH must only handle data errors. all other errors must not be caught */\
if(ERRCODE_TO_CATEGORY(elog_geterrcode()) != ERRCODE_DATA_EXCEPTION)\
{\
	PG_RE_THROW(); \
}

#define HANDLE_SINGLE_ROW_ERROR(pstate) \
if (pstate->errMode == ALL_OR_NOTHING) \
{ \
	/* re-throw error and abort */ \
	PG_RE_THROW(); \
} \
else \
{ \
	/* SREH - release error state */ \
\
	ErrorData	*edata; \
	MemoryContext oldcontext;\
\
	/* save a copy of the error info */ \
	oldcontext = MemoryContextSwitchTo(pstate->cdbsreh->badrowcontext);\
	edata = CopyErrorData();\
\
	if (!elog_dismiss(DEBUG5)) \
		PG_RE_THROW(); /* <-- hope to never get here! */ \
\
	truncateEol(&pstate->line_buf, pstate->eol_type); \
	pstate->cdbsreh->rawdata = pstate->line_buf.data; \
	pstate->cdbsreh->is_server_enc = pstate->line_buf_converted; \
	pstate->cdbsreh->linenumber = pstate->cur_lineno; \
	pstate->cdbsreh->processed++; \
\
	/* set the error message. Use original msg and add column name if available */ \
	if (pstate->cur_attname)\
	{\
		pstate->cdbsreh->errmsg = psprintf("%s, column %s", \
				edata->message, \
				pstate->cur_attname); \
	}\
	else\
	{\
		pstate->cdbsreh->errmsg = pstrdup(edata->message); \
	}\
\
	HandleSingleRowError(pstate->cdbsreh); \
	FreeErrorData(edata);\
	if (!IsRejectLimitReached(pstate->cdbsreh)) \
		pfree(pstate->cdbsreh->errmsg); \
	MemoryContextSwitchTo(oldcontext);\
}

#define FILEAM_HANDLE_ERROR(pstate) \
CHECK_DATA_EXCEPTION; \
HANDLE_SINGLE_ROW_ERROR(pstate)

#endif   /* FILEAM_H */
