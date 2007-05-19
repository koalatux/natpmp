#define NATPMP_VERSION 0
#define NATPMP_ANSFLAG 0x80
#define NATPMP_PUBLICIPADDRESS 0
#define NATPMP_MAP_UDP 1
#define NATPMP_MAP_TCP 2
#define NATPMP_SUCCESS 0
#define NATPMP_UNSUPPORTEDVERSION 1
#define NATPMP_REFUSED 2
#define NATPMP_NETFAILURE 3
#define NATPMP_OUTOFRESOURCES 4
#define NATPMP_UNSUPPORTEDOP 5

#define NATPMP_MAX_PAYLOADSIZE 112


typedef struct {
	uint8_t version;
	uint8_t op;
} _natpmp_header;

typedef struct{
	uint16_t result;
	uint32_t epoch;
} _natpmp_answer;

typedef struct{
	in_port_t private_port;
	in_port_t public_port;
	uint32_t lifetime;
} _natpmp_mapping;


typedef struct {
	_natpmp_header header;
	char _data[NATPMP_MAX_PAYLOADSIZE];
} natpmp_packet;

typedef struct {
	_natpmp_header header;
} natpmp_packet_publicipaddress_request;

typedef struct {
	_natpmp_header header;
	_natpmp_answer answer;
	in_addr_t public_ip_address;
} natpmp_packet_publicipaddress_answer;

typedef struct {
	_natpmp_header header;
	uint16_t _reserved;
	_natpmp_mapping mapping;
} natpmp_packet_map_request;

typedef struct {
	_natpmp_header header;
	_natpmp_answer answer;
	_natpmp_mapping mapping;
} natpmp_packet_map_answer;
