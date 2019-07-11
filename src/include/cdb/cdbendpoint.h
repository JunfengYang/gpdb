/*
 * cdbendpoint.h
 *
 * Copyright (c) 2018-Present Pivotal Software, Inc.
 */

#ifndef CDBENDPOINT_H
#define CDBENDPOINT_H

#include <inttypes.h>

#include "cdb/cdbutil.h"
#include "nodes/parsenodes.h"
#include "storage/latch.h"
#include "tcop/dest.h"
#include "storage/dsm.h"
#include "storage/shm_mq.h"

#define InvalidToken (-1)
#define InvalidTokenIndex (-1)
#define InvalidSession (-1)
#define DummyToken			(0) /* For fault injection */

#define MAX_ENDPOINT_SIZE	1024
#define POLL_FIFO_TIMEOUT	50
#define SHMEM_TOKEN "SharedMemoryToken"
#define SHMEM_TOKEN_SLOCK "SharedMemoryTokenSlock"
#define SHMEM_END_POINT "SharedMemoryEndpoint"
#define SHMEM_END_POINT_SLOCK "SharedMemoryEndpointSlock"

#define GP_ENDPOINT_STATUS_INIT		  "INIT"
#define GP_ENDPOINT_STATUS_READY	  "READY"
#define GP_ENDPOINT_STATUS_RETRIEVING "RETRIEVING"
#define GP_ENDPOINT_STATUS_FINISH	  "FINISH"
#define GP_ENDPOINT_STATUS_RELEASED   "RELEASED"

#define GP_ENDPOINTS_INFO_ATTRNUM 8

enum EndpointRole
{
	EPR_SENDER = 1,
	EPR_RECEIVER,
	EPR_NONE
};

enum RetrieveStatus
{
	RETRIEVE_STATUS_INVALID,
	RETRIEVE_STATUS_INIT,
	RETRIEVE_STATUS_GET_TUPLEDSCR,
	RETRIEVE_STATUS_GET_DATA,
	RETRIEVE_STATUS_FINISH,
};

typedef enum AttachStatus
{
	Status_NotAttached = 0,
	Status_Prepared,
	Status_Attached,
	Status_Finished
}	AttachStatus;

/*
 * SharedTokenDesc is a entry to store the information of a token, includes:
 * token: token number
 * cursor_name: the parallel cursor's name
 * session_id: which session created this parallel cursor
 * endpoint_cnt: how many endpoints are created.
 * all_seg: a flag to indicate if the endpoints are on all segments.
 * dbIds: a bitmap stores the dbids of every endpoint, size is 4906 bits(32X128).
 */
#define MAX_NWORDS 128
typedef struct sharedtokendesc
{
	int64		token;
	char		cursor_name[NAMEDATALEN];
	int			session_id;
	int			endpoint_cnt;
	Oid			user_id;
	bool		all_seg;
	int32		dbIds[MAX_NWORDS];
}	SharedTokenDesc;

typedef struct EndpointDesc
{
    Oid			database_id;
    pid_t		sender_pid;
    pid_t		receiver_pid;
    int64		token;
    dsm_handle	handle;
    Latch		ack_done;
    AttachStatus attach_status;
    int			session_id;
    Oid			user_id;
    bool		empty;
}	EndpointDesc;

typedef SharedTokenDesc *SharedToken;
typedef EndpointDesc *Endpoint;

typedef struct
{
	DestReceiver pub;			/* publicly-known function pointers */
}	DR_mq_printtup;

typedef struct MessageQueueData
{
	dsm_segment*   mq_seg;
	shm_mq_handle* mq_handle;
	bool		   finished;
}	MessageQueueData;

typedef struct
{
	int64		token;
	int			dbid;
	AttachStatus attach_status;
	pid_t		sender_pid;
}	EndpointStatus;

typedef struct
{
	int			curTokenIdx;
	/* current index in shared token list. */
	CdbComponentDatabaseInfo *seg_db_list;
	int			segment_num;
	/* number of segments */
	int			curSegIdx;
	/* current index of segment id */
	EndpointStatus *status;
	int			status_num;
}	EndpointsInfo;

typedef struct
{
	int			endpoints_num;
	/* number of endpointdesc in the list */
	int			current_idx;
	/* current index of endpointdesc in the list */
}	EndpointsStatusInfo;

/* Shared memory context and dsm create/attach */
extern Size Endpoint_ShmemSize(void);
extern void Endpoint_CTX_ShmemInit(void);
extern void AttachOrCreateEndpointAndTokenDSM(void);
extern bool AttachOrCreateEndpointDsm(bool attachOnly);
extern bool AttachOrCreateTokenDsm(bool attachOnly);

/* Declare parallel cursor */
extern int64 GetUniqueGpToken(void);
extern void AddParallelCursorToken(int64 token, const char *name, int session_id, Oid user_id, bool all_seg, List *seg_list);

/* Execute parallel cursor, start sender job */
extern DestReceiver *CreateEndpointReceiver(void);
extern DestReceiver *CreateTQDestReceiverForEndpoint(TupleDesc tupleDesc);
extern void DestroyTQDestReceiverForEndpoint(DestReceiver *endpointDest);

/* Execute parallel cursor finish, unset pid and exit retrieve if needed */
extern void UnsetSenderPidOfToken(int64 token);

/* Remove parallel cursor Parallel cursor drop/abort */
extern void RemoveParallelCursorToken(int64 token);

/* Endpoint backend register/free, execute on backend(QE or QD) */
extern void AllocEndpointOfToken(int64 token); /* Normally the endpoint is on QE, buf for some case, it's on QD */
extern void FreeEndpointOfToken(int64 token); // TODO: make it private
extern void assign_gp_endpoints_token_operation(const char *newval, void *extra);


/* Retrieve role auth */
extern bool FindEndpointTokenByUser(Oid user_id, const char *token_str);

/* For retrieve role. Must have endpoint allocated */
extern void AttachEndpoint(void);
extern TupleDesc TupleDescOfRetrieve(void);
extern void RetrieveResults(RetrieveStmt * stmt, DestReceiver *dest);
extern void DetachEndpoint(bool reset_pid);
extern void AbortEndpoint(void);


/* Utilities */
extern int64 GpToken(void);
extern void SetGpToken(int64 token);
extern void ClearGpToken(void);
extern int64 parseToken(char *token);
extern char* getTokenNameFormatStr(void); // TODO: Should be private
extern char* printToken(int64 token_id); /* Need to pfree() the result */

extern void SetEndpointRole(enum EndpointRole role);
extern void ClearEndpointRole(void);
extern enum EndpointRole EndpointRole(void);

extern List *GetContentIDsByToken(int64 token);


/* UDFs for endpoint */
extern Datum gp_endpoints_info(PG_FUNCTION_ARGS);
extern Datum gp_endpoints_status_info(PG_FUNCTION_ARGS);

#endif   /* CDBENDPOINT_H */
