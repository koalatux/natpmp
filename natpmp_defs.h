#define NATPMP_VERSION 0
#define NATPMP_OP_ANSOFFSET 128
#define NATPMP_OP_REQ_PUBLICIPADDRESS 0
#define NATPMP_OP_REQ_MAP_UDP 1
#define NATPMP_OP_REQ_MAP_TCP 2
#define NATPMP_OP_ANS_PUBLICIPADDRESS (NATPMP_OP_ANSOFFSET+NATPMP_OP_REQ_PUBLICIPADDRESS)
#define NATPMP_OP_ANS_MAP_UDP (NATPMP_OP_ANSOFFSET+NATPMP_OP_REQ_MAP_UDP)
#define NATPMP_OP_ANS_MAP_TCP (NATPMP_OP_ANSOFFSET+NATPMP_OP_REQ_MAP_TCP)
#define NATPMP_RESULT_SUCCESS 0
#define NATPMP_RESULT_UNSUPPORTEDVERSION 1
#define NATPMP_RESULT_REFUSED 2
#define NATPMP_RESULT_NETFAILURE 3
#define NATPMP_RESULT_OUTOFRESOURCES 4
#define NATPMP_RESULT_UNSUPPORTEDOP 5

typedef struct {
	uint8 version;
	uint8 op;
} natpmp_packet_simple;

typedef struct {
	uint8 version;
	uint8 op;
	uint16 result;
	uint32 epoch;
	uint32 public_ip_address;
} natpmp_packet_publicipaddress_answer;

typedef struct {
	uint8 version;
	uint8 op;
	uint16 reserved;
	uint16 private_port;
	uint16 public_port;
	uint32 lifetime;
} natpmp_packet_map_request;

typedef struct {
	uint8 version;
	uint8 op;
	uint16 result;
	uint32 epoch;
	uint16 private_port;
	uint16 public_port;
	uint32 lifetime;
} natpmp_packet_map_answer;
