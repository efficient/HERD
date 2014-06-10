#include "common.h"

union ibv_gid get_gid(struct ibv_context *context)
{
	union ibv_gid ret_gid;
	ibv_query_gid(context, IB_PHYS_PORT, 0, &ret_gid);

	fprintf(stderr, "GID: Interface id = %lld subnet prefix = %lld\n", 
		(long long) ret_gid.global.interface_id, 
		(long long) ret_gid.global.subnet_prefix);
	
	return ret_gid;
}

uint16_t get_local_lid(struct ibv_context *context)
{
	struct ibv_port_attr attr;

	if (ibv_query_port(context, IB_PHYS_PORT, &attr))
		return 0;

	return attr.lid;
}

int close_ctx(struct ctrl_blk *ctx)
{
	int i;
	for(i = 0; i < ctx->num_conn_qps; i++) {
		if (ibv_destroy_qp(ctx->conn_qp[i])) {
			fprintf(stderr, "Couldn't destroy connected QP\n");
			return 1;
		}
		if (ibv_destroy_cq(ctx->conn_cq[i])) {
			fprintf(stderr, "Couldn't destroy connected CQ\n");
			return 1;
		}
	}
	
	for(i = 0; i < ctx->num_local_dgram_qps; i++) {
		if (ibv_destroy_qp(ctx->dgram_qp[i])) {
			fprintf(stderr, "Couldn't destroy datagram QP\n");
			return 1;
		}
		if (ibv_destroy_cq(ctx->dgram_cq[i])) {
			fprintf(stderr, "Couldn't destroy datagarm CQ\n");
			return 1;
		}
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx);

	return 0;
}

void create_qp(struct ctrl_blk *ctx)
{
	int i;

	// Create connected queue pairs
	ctx->conn_qp = malloc(sizeof(int *) * ctx->num_conn_qps);
	ctx->conn_cq = malloc(sizeof(int *) * ctx->num_conn_qps);

	for(i = 0; i < ctx->num_conn_qps; i++) {
		ctx->conn_cq[i] = ibv_create_cq(ctx->context, 
			Q_DEPTH + 1, NULL, NULL, 0);
		CPE(!ctx->conn_cq[i], "Couldn't create connected CQ", 0);
	
		struct ibv_qp_init_attr conn_init_attr = {
			.send_cq = ctx->conn_cq[i],
			.recv_cq = ctx->conn_cq[i],
			.cap     = {
				.max_send_wr  = Q_DEPTH,
				.max_recv_wr  = 1,
				.max_send_sge = 1,
				.max_recv_sge = 1,
				.max_inline_data = S_KV < 256 ? S_KV : 256
			},
			.qp_type = USE_UC ? IBV_QPT_UC : IBV_QPT_RC
		};
		ctx->conn_qp[i] = ibv_create_qp(ctx->pd, &conn_init_attr);
		CPE(!ctx->conn_qp[i], "Couldn't create connected QP", 0);
	}

	// Create datagram queue pairs
	ctx->dgram_qp = malloc(sizeof(int *) * ctx->num_local_dgram_qps);
	ctx->dgram_cq = malloc(sizeof(int *) * ctx->num_local_dgram_qps);

	for(i = 0; i < ctx->num_local_dgram_qps; i++) {
		ctx->dgram_cq[i] = ibv_create_cq(ctx->context, 
			Q_DEPTH + 1, NULL, NULL, 0);
		CPE(!ctx->dgram_cq[i], "Couldn't create datagram CQ", 0);

		struct ibv_qp_init_attr dgram_init_attr = {
			.send_cq = ctx->dgram_cq[i],
			.recv_cq = ctx->dgram_cq[i],
			.cap     = {
				.max_send_wr  = Q_DEPTH,
				.max_recv_wr  = Q_DEPTH,
				.max_send_sge = 1,
				.max_recv_sge = 1,
				.max_inline_data = S_KV < 256 ? S_KV : 256
			},
		.qp_type = IBV_QPT_UD
		};
		ctx->dgram_qp[i] = ibv_create_qp(ctx->pd, &dgram_init_attr);
		CPE(!ctx->dgram_qp[i], "Couldn't create datagram QP", 0);
	}
}

// Modify all the UD and RC/UC queue pairs to the init stage
void modify_qp_to_init(struct ctrl_blk *ctx)
{
	int i;
	struct ibv_qp_attr dgram_attr = {
		.qp_state		= IBV_QPS_INIT,
		.pkey_index		= 0,
		.port_num		= IB_PHYS_PORT,
		.qkey 			= 0x11111111
	};

	for(i = 0; i < ctx->num_local_dgram_qps; i++) {
		if (ibv_modify_qp(ctx->dgram_qp[i], &dgram_attr,
			IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY)) {
			fprintf(stderr, "Failed to modify dgram. QP to INIT\n");
			return;
		}
	}

	struct ibv_qp_attr conn_attr = {
		.qp_state		= IBV_QPS_INIT,
		.pkey_index		= 0,
		.port_num		= IB_PHYS_PORT,
		.qp_access_flags= IBV_ACCESS_REMOTE_WRITE
	};

	for(i = 0; i < ctx->num_conn_qps; i++) {
		if (ibv_modify_qp(ctx->conn_qp[i], &conn_attr,
			IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify conn. QP to INIT\n");
			return;
		}
	}
}

// Create RDMA request and response regions for the clients and the server
int setup_buffers(struct ctrl_blk *cb)
{
	int FLAGS = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | 
				IBV_ACCESS_REMOTE_WRITE;
	if (cb->is_client) {
		// Create and register the response region for a client
		client_resp_area = (struct UD_KV *) memalign(4096, RESP_AC * S_UD_KV);
		client_resp_area_mr = ibv_reg_mr(cb->pd, 
			(char *)client_resp_area, RESP_AC * S_UD_KV, FLAGS);
		CPE(!client_resp_area_mr, "client_req reg_mr failed", errno);

		// Register the client's request to allow non-inlined requests
		client_req_area = (struct KV *) memalign(4096, WINDOW_SIZE * S_KV);
		client_req_area_mr = ibv_reg_mr(cb->pd, 
			(char *) client_req_area, WINDOW_SIZE * S_KV, FLAGS);
			
		cb->wr.opcode = IBV_WR_RDMA_WRITE;
		cb->sgl.lkey = client_req_area_mr->lkey;
		
	} else {
		if(REQ_AC * S_KV > M_2) {
			fprintf(stderr, "Request area larger than 1 hugepage. Exiting.\n");
			exit(-1);
		}

		if(cb->id == 0) {
			// Create and register master server's request region
			int sid = shmget(REQ_AREA_SHM_KEY, M_2, IPC_CREAT | 0666 | SHM_HUGETLB);
			CPE(sid < 0, "Master server request area shmget() failed\n", sid);

			server_req_area = shmat(sid, 0, 0);
			memset((char *) server_req_area, 0, M_2);
			server_req_area_mr = ibv_reg_mr(cb->pd, (char *)server_req_area, M_2, FLAGS);
			CPE(!server_req_area_mr, "Failed to register server's request area", errno);

			// Create and register master server's response region
			sid = shmget(RESP_AREA_SHM_KEY, M_2, IPC_CREAT | 0666 | SHM_HUGETLB);
			CPE(sid < 0, "Master server response area shmget() failed\n", sid);
			server_resp_area = shmat(sid, 0, 0);
			memset((char *) server_resp_area, 0, M_2);
			server_resp_area_mr = ibv_reg_mr(cb->pd, (char *)server_resp_area, M_2, FLAGS);
			CPE(!server_resp_area_mr, "Failed to register server's response area", errno);
		} else {
			// For a slave server, map and register the regions created by the master
			int sid = shmget(REQ_AREA_SHM_KEY, REQ_AC * S_KV, SHM_HUGETLB | 0666);
			server_req_area = shmat(sid, 0, 0);
			server_req_area_mr = ibv_reg_mr(cb->pd, (char *) server_req_area, M_2, FLAGS);

			sid = shmget(RESP_AREA_SHM_KEY, REQ_AC * S_KV, SHM_HUGETLB | 0666);
			server_resp_area = shmat(sid, 0, 0);
			server_resp_area_mr = ibv_reg_mr(cb->pd, (char *) server_resp_area, M_2, FLAGS);
		}

		cb->wr.opcode = IBV_WR_SEND;
		cb->sgl.lkey = server_resp_area_mr->lkey;
	}

	cb->wr.sg_list = &cb->sgl;
	cb->wr.num_sge = 1;

	return 0;
}

void print_kv(struct KV kv)
{
	int i;
	fflush(stdout);
	fprintf(stdout, "Key: %llx ~ %d ", kv.key[0], (char) kv.key[0]);
	#if(KEY_SIZE == 2)
		fprintf(stdout, "%d ", (char) kv.key[1]);	
	#endif

	fprintf(stdout, "Length: %d Value: ", kv.len);
	for(i = 0; i < 3; i++) {
		fprintf(stdout, "%d ", kv.value[i]);
	}
	fprintf(stdout, "\n");
}

void print_ud_kv(struct UD_KV ud_kv)
{
	fflush(stdout);
	print_kv(ud_kv.kv);
}

void print_stag(struct stag st)
{
	fflush(stdout);
	fprintf(stderr, "\t%lu, %u, %u\n", st.buf, st.rkey, st.size); 
}

void print_qp_attr(struct qp_attr dest)
{
	fflush(stdout);
	fprintf(stderr, "\t%d %d %d\n", dest.lid, dest.qpn, dest.psn);
}

void print_kv_array(volatile struct KV *kv, int size)
{
	int i;
	fflush(stdout);
	for(i = 0; i < size; i++) {
		fprintf(stderr, "\t%d ", i);
		print_kv(kv[i]);
	}
}

void print_ud_kv_array(struct UD_KV *ud_kv, int size)
{
	int i;
	fflush(stdout);
	for(i = 0; i < size; i++) {
		fprintf(stderr, "\t%d ", i);
		print_ud_kv(ud_kv[i]);
	}
}

inline long long get_cycles()
{
	unsigned low, high;
	unsigned long long val;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	val = high;
	val = (val << 32) | low;
	return val;
}

void nano_sleep(int ns)
{
	long long start = get_cycles();
	long long end = start;
	int upp = (int) (2.1 * ns);
	while(end - start < upp) {
		end = get_cycles();
	}
}

// Poll the UD completion queue indexed cq_num for num_completions completions
void poll_dgram_cq(int num_completions, struct ctrl_blk *cb, int cq_num)
{
	struct ibv_wc wc;
	int comps= 0;
	while(comps < num_completions) {
		int new_comps = ibv_poll_cq(cb->dgram_cq[cq_num], 1, &wc);
		if(new_comps != 0) {
			comps += new_comps;
			if(wc.status != 0) {
				fprintf(stderr, "Bad wc status %d\n", wc.status);
				exit(0);
			}
		}
	}
}

// Poll the UC completion queue indexed cq_num for num_completions completions
void poll_conn_cq(int num_completions, struct ctrl_blk *cb, int cq_num)
{
	struct ibv_wc wc;
	int comps= 0;
	while(comps < num_completions) {
		int new_comps = ibv_poll_cq(cb->conn_cq[cq_num], 1, &wc);
		if(new_comps != 0) {
			comps += new_comps;
			if(wc.status != 0) {
				fprintf(stderr, "Bad wc status %d\n", wc.status);
			}
		}
	}
}

// Check if the GET result (val) matches the expected key (exp)
int valcheck(volatile char *val, LL exp) 
{
	int i = 0, ok = 1;
	for(i = 0; i < VALUE_SIZE; i++) {
		if(val[i] != (char) exp) {
			ok = 0;
			break;
		}
	}

	if(!ok) {
		fprintf(stderr, "Expected:\n");
		for(i = 0; i < VALUE_SIZE; i++) {
			fprintf(stderr, "%d ", (char) exp);
		}
		fprintf(stderr, "\n");

		fprintf(stderr, "Received:\n");
		for(i = 0; i < VALUE_SIZE; i++) {
			fprintf(stderr, "%d ", val[i]);
		}
		fprintf(stderr, "\n");
	}
	return ok;		// TRUE
}

void print_ht_index()
{
	int bkt_i, slot_i;
	for(bkt_i = 0; bkt_i < NUM_IDX_BKTS; bkt_i ++) {
		for(slot_i = 0; slot_i < 8; slot_i ++) {
			LL offset = SLOT_TO_OFFSET(ht_index[bkt_i].slot[slot_i]);
			if(offset < 0) {
				offset = -1;
			}
			fprintf(stdout, "%d|%lld\t", 
				SLOT_TO_TAG(ht_index[bkt_i].slot[slot_i]), offset);
		}
		fprintf(stdout, "\n");
	}
}

// Generate the keys for client number cn
LL* gen_key_corpus(int cn)
{
	int key_i;
	LL temp;
	
	LL *key_corpus = (LL *) malloc(NUM_KEYS * sizeof(LL));

	if(ZIPF == 1) {
		char filename[160];			// Keep it large, just in case
		sprintf(filename, "/dev/zipf/data%d.dat", cn);
		printf("Scanning ZIPF keys from file %s.\n", filename);
		FILE *keys_fp = fopen(filename, "r");
		if(keys_fp == NULL) {
			fprintf(stderr, "Couldn't open zipf keys for reading\n");
			exit(1);
		}
		for(key_i = 0; key_i < NUM_KEYS; key_i ++) {
			fscanf(keys_fp, "%lld", &temp);
			key_corpus[key_i] = temp;
		}
	} else {
		for(key_i = 0; key_i < NUM_KEYS; key_i ++) {
			LL rand1 = (LL) lrand48();
			LL rand2 = (LL) lrand48();
			key_corpus[key_i] = (rand1 << 32) ^ rand2;
			if((char) key_corpus[key_i] == 0) {		// No 0 keys
				key_i --;
			}
		}
	}

	return key_corpus;
}

void init_ht(struct ctrl_blk *cb)
{
	int bkt_i, slot_i;				// Indices for buckets and slots
	int shm_flags = IPC_CREAT | 0666;
	if(USE_HUGEPAGE) {
		shm_flags |= SHM_HUGETLB;
	}
	int ht_index_sid = shmget(BASE_HT_INDEX_SHM_KEY + cb->id, 
		NUM_IDX_BKTS * S_IDX_BKT, shm_flags);
	if(ht_index_sid == -1) {
		fprintf(stderr, "shmget Error! Failed to create index\n");
		system("cat /sys/devices/system/node/*/meminfo | grep Huge");
		exit(0);
	}
	
	ht_index = (struct IDX_BKT *) shmat(ht_index_sid, 0, 0);
	for(bkt_i = 0; bkt_i < NUM_IDX_BKTS; bkt_i ++) {
		for(slot_i = 0; slot_i < SLOTS_PER_BKT; slot_i ++) {
			ht_index[bkt_i].slot[slot_i] = INVALID_SLOT;
		}		
	}

	int ht_log_sid = shmget(BASE_HT_LOG_SHM_KEY + cb->id,
		LOG_SIZE, shm_flags);
	if(ht_log_sid == -1) {
		fprintf(stderr, "shmget Error! Failed to create circular log\n");
		system("cat /sys/devices/system/node/*/meminfo | grep Huge");
		exit(0);
	}	
	ht_log = shmat(ht_log_sid, 0, 0);
	memset(ht_log, 0, LOG_SIZE);
}

int is_roce(void)
{
	char *env = getenv("ROCE");
	if(env == NULL) {		// If ROCE environment var is not set
		fprintf(stderr, "ROCE not set\n");
		exit(-1);
	}
	return atoi(env);
}

inline uint32_t fastrand(uint64_t* seed)
{
    *seed = *seed * 1103515245 + 12345;
    return (uint32_t)(*seed >> 32);
}
