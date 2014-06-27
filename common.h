#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <infiniband/verbs.h>
#include "sizes.h"

#define FAIL_LIM 100			// # of failed polls before a server advances its pipeline
#define ZIPF 0					// Use ZIPF distributed workload
#define KEY_SIZE 2				// In long long units
#define VALUE_SIZE 32			// In char units
#define SLOTS_PER_BKT 8			// Number of <tag, pointer> pairs in a MICA-style index bkt

#define PUT_PERCENT 5			// Percentage of PUT operations

#define IB_PHYS_PORT 1			// Primary physical port number for qps
#define CLIENT_PRINT_LAT 0		// Should clients sample request latency?

#define USE_UC 1				// Use UC for requests. If 0, RC is ued
#define USE_INLINE 1			// Use WQE inlining for requests and responses
#define USE_HUGEPAGE 1

#if (USE_INLINE == 0)
	#define MY_SEND_INLINE 0
#else
	#define MY_SEND_INLINE IBV_SEND_INLINE
#endif

#define NUM_CLIENTS 36			// Number of client processes
#define NUM_SERVERS 7			// Number of server processes

#define Q_DEPTH 1024			// Size of all created queues
#define S_DEPTH 512
#define S_DEPTH_ 511

/**** Performance params that need to be tuned with VALUE_SIZE ****/
#define WINDOW_SIZE 4			// Outstanding requests by a client
#define WINDOW_SIZE_ 3

#define WS_SERVER 64			// Outstanding responses by a server
#define WS_SERVER_ 63
/******************************************************************/

#define CL_BTCH_SZ 128			// Number of RECVs maintained by a client
#define CL_BTCH_SZ_ 127

#define CL_SEMI_BTCH_SZ 64		// Client posts new RECVs if the number drops below this
#define CL_SEMI_BTCH_SZ_ 63

#define NUM_ITER 1000000000		// Total number of iterations performed by a client

#define REQ_AC (NUM_CLIENTS * WINDOW_SIZE * NUM_SERVERS)
#define RESP_AC (WINDOW_SIZE * NUM_SERVERS)

// SHM keys for servers. The request and response area is shared among server processes
// and needs a single key. For lossy index and circular log, each server uses a separate
// key := BASE + cb->id. Number of server processes must be less than 32.
#define REQ_AREA_SHM_KEY 3185
#define RESP_AREA_SHM_KEY 3186
#define BASE_HT_INDEX_SHM_KEY 1
#define BASE_HT_LOG_SHM_KEY 32

// The number of keys written by each client process
#define NUM_KEYS M_1			// 51 * M_4 ~ 200 M keys
#define NUM_KEYS_ M_1_

// Number of index buckets (each with 8 slots) at each server process
#define NUM_IDX_BKTS M_4		// Size = 256M. Support for 32M keys
#define NUM_IDX_BKTS_ M_4_

// The size of the circular at each server process
#define LOG_SIZE M_128			// Support for 32M keys
#define LOG_SIZE_ M_128_

// Compare, print, and exit
#define CPE(val, msg, err_code) \
	if(val) { fprintf(stderr, msg); fprintf(stderr, " Error %d \n", err_code); \
	exit(err_code);}

// The key-value struct
struct __attribute__((__packed__)) KV {
	uint16_t len;
	char value[VALUE_SIZE];
	long long key[KEY_SIZE];	// <-- KV_KEY_OFFSET
};
#define S_KV sizeof(struct KV)
#define KV_KEY_OFFSET (2 + VALUE_SIZE)

// For a RECV completion, clients get the KV and the UD GRH
struct UD_KV {
	char grh[40];
	struct KV kv;
};
#define S_UD_KV sizeof(struct UD_KV)

// Operation types for pipeline items
#define GET_TYPE 81
#define PUT_TYPE 82
#define DUMMY_TYPE 83
#define EMPTY_TYPE 84

// If the client receives len < GET_FAIL_LEN_1 for a GET request, it assumes
// that the request succeeded and checks the value. PUT requests always succeed.
#define GET_FAIL_LEN_1 20001	// Denotes GET failure in pipeline stage 1
#define GET_FAIL_LEN_2 20002	// Denotes GET failure in pipeline stage 2

// PIpeline ITem
struct PL_IT {
	struct KV *kv;
	int cn;				// Client who sent the KV
	int req_area_slot;	// Request area slot into which this request was received
	int get_slot;
	int req_type;
	// The rightmost part of KV that must be zeroed immediately after detecting
	// a new request. If this is not done, the server can loop around the window
	// and detect an old request again
	long long poll_val;
};
#define S_PL_IT sizeof(struct PL_IT)
struct PL_IT pipeline[2];		// The pipeline

struct IDX_BKT {		// An index bucket
	long long slot[SLOTS_PER_BKT];
};
#define S_IDX_BKT sizeof(struct IDX_BKT)

// A slot contains a 6 byte offset and a 2 byte tag.
// Invalid slots have their MSB set to 1.
#define INVALID_SLOT 0x8000000000000000
#define MIN_INT 0x80000000
#define MIN_LL 0x8000000000000000

struct qp_attr {
	uint64_t gid_global_interface_id;	// Store the gid fields separately because I
	uint64_t gid_global_subnet_prefix; 	// don't like unions. Needed for RoCE only

	int lid;							// A queue pair is identified by the local id (lid)
	int qpn;							// of the device port and its queue pair number (qpn)
	int psn;
};
#define S_QPA sizeof(struct qp_attr)

struct ctrl_blk {
	struct ibv_context *context;
	struct ibv_pd *pd;

	struct ibv_cq **conn_cq;				// Queue pairs and completion queues
	struct ibv_qp **conn_qp;
	
	struct ibv_cq **dgram_cq;
	struct ibv_qp **dgram_qp;

	struct ibv_ah *ah[NUM_CLIENTS];			// Per client address handles
	
	struct qp_attr *local_dgram_qp_attrs;	// Local and remote queue pair attributes
	struct qp_attr *remote_dgram_qp_attrs;

	struct qp_attr *local_conn_qp_attrs;
	struct qp_attr *remote_conn_qp_attrs;
	
	struct ibv_send_wr wr;					// A work request and its scatter-gather list
	struct ibv_sge sgl;

	int num_conn_qps, num_remote_dgram_qps, num_local_dgram_qps;
	int is_client, id;
	int sock_port;							// The socket port a server uses for client connections
};

struct stag {								// An "stag" identifying an RDMA region
	uint64_t buf;
	uint32_t rkey;
	uint32_t size;
};
#define S_STG sizeof(struct stag)

// The lossy index and the circular log
struct IDX_BKT *ht_index;
char *ht_log;

// Request and response regions, and their RDMA memory-region descriptors
volatile struct KV *server_req_area;
volatile struct KV *server_resp_area;
volatile struct KV *client_req_area;
volatile struct UD_KV *client_resp_area;

struct ibv_mr *server_req_area_mr, *server_resp_area_mr;
struct ibv_mr *client_resp_area_mr, *client_req_area_mr;

struct stag server_req_area_stag[NUM_SERVERS], client_resp_area_stag[NUM_CLIENTS];

union ibv_gid get_gid(struct ibv_context *context);
uint16_t get_local_lid(struct ibv_context *context);

void create_qp(struct ctrl_blk *ctx);
void modify_qp_to_init(struct ctrl_blk *ctx);
int setup_buffers(struct ctrl_blk *cb);

void client_exch_dest(struct ctrl_blk *ctx);
void server_exch_dest(struct ctrl_blk *ctx);

int modify_dgram_qp_to_rts(struct ctrl_blk *ctx);
int connect_ctx(struct ctrl_blk *ctx, int my_psn, struct qp_attr dest, int qp_i);

int close_ctx(struct ctrl_blk *ctx);

void print_kv(struct KV);
void print_ud_kv(struct UD_KV);
void print_stag(struct stag);
void print_qp_attr(struct qp_attr);
void print_kv_array(volatile struct KV *kv, int size);
void print_ud_kv_array(struct UD_KV *ud_kv, int size);
void print_ht_index();

void nano_sleep(int ns);
inline long long get_cycles();

void poll_conn_cq(int num_completions, struct ctrl_blk *cb, int cq_num);
void poll_dgram_cq(int num_completions, struct ctrl_blk *cb, int cq_num);

int valcheck(volatile char *val, long long exp);

long long* gen_key_corpus(int cn);
void init_ht(struct ctrl_blk *cb);
int is_roce(void);
inline uint32_t fastrand(uint64_t* seed);

#define LL long long
#define KEY_TO_BUCKET(k) ((int) (k >> 16) & NUM_IDX_BKTS_)		// 3 bytes (up to 16 Mi buckets)
#define KEY_TO_TAG(k) ((int) (k & 0xffff))						// 2 bytes
#define KEY_TO_SERVER(k) ((int) ((k >> 40) % (NUM_SERVERS - 1) + 1))	// DO NOT shift by 48

#define SLOT_TO_OFFSET(s) (s >> 16)
#define SLOT_TO_TAG(s) ((int) (s & 0xffff))

#define SET_PL_IT_MEMCPY_DONE(pl_it) (pl_it.cn |= 0xf00)
#define GET_PL_IT_MEMCPY_DONE(pl_it) (pl_it->cn & 0xf00)
