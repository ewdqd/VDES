#ifndef VDES_GLOBAL_H
#define VDES_GLOBAL_H

#include "socket_utils.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef int8_t  rt_int8_t;
typedef int16_t rt_int16_t;
typedef int32_t rt_int32_t;

#define MES_BUF_MAX 10
#define SHORT_MES_MAX 23
#define LINK_ID_NUM 12
#define SEGMENT_COUNT 31
#define DC_SEGMENT_COUNT 6
#define ASC_MAX_NUM 5
#define RAC_TOTAL_SLOTS 170
#define RAC_STEP 5
#define NUM_VALUES 35
#define SLOT_TOTAL 2250

#define LOGIC_CH_A 0x0
#define LOGIC_CH_B 0x1
#define LOGIC_CH_C 0x2
#define LOGIC_CH_D 0x3
#define LOGIC_CH_E 0x4
#define LOGIC_CH_F 0x5

#define MSG_STATE_FULL 0xAA
#define MSG_STATE_FILL 0x55

#define SEG_UNUSED 0
#define SEG_USED 0xAA

#define LINKID20 20

#define MY_MMSI 200000002UL
#define SAT_ID 99

#define PHY_CH_A 0x0A
#define PHY_CH_B 0x0B
#define PHY_CH_C 0x0C
#define PHY_CH_D 0x0D
#define PHY_CH_E 0x0E
#define PHY_CH_F 0x0F

extern int g_currentSlot;
void setGlobalCurrentSlot(int slot);
int getGlobalCurrentSlot();

#pragma pack(push, 1)

typedef struct {
    rt_uint32_t ship_id;
    rt_uint32_t ack;
    rt_uint8_t type;
    rt_uint8_t data;
    uint16_t link_id;
    rt_uint8_t message[SHORT_MES_MAX];
    rt_int32_t lenth;
    rt_int32_t send_state;
    rt_int32_t state;
    rt_int32_t ID_send_times;
} Modu_2send_short;

typedef struct {
    rt_uint8_t type;
    rt_uint8_t src_ID[4];
    rt_uint8_t stationID[4];
    rt_uint8_t data;
} UplinkShortMessageNoAck;

typedef struct {
    rt_uint8_t type;
    rt_uint8_t src_ID[4];
    rt_uint8_t stationID[4];
    rt_uint8_t data;
} UplinkShortMessageAck;

typedef struct {
    rt_uint8_t type;
    rt_uint8_t sourceID[4];
    rt_uint8_t data[5];
} RAC_DownSMsgNoAckid;

typedef struct {
    rt_uint32_t Mode_start;
    rt_uint32_t random_interval;
    rt_uint32_t rac_start_slot;
    rt_uint32_t rac_slot_offset;
    rt_uint32_t rac_message_cnt;
    rt_uint32_t cnt_15min;
    rt_uint8_t down_cqi;
    rt_uint8_t Long_mes_process_cnt;
    rt_uint8_t handler_index;
} GL_event_flag;

typedef struct {
    rt_uint8_t type;
    rt_uint16_t payloadSize;
    rt_uint8_t satID;
    rt_uint8_t mainNetID;
    rt_uint8_t roamNetID;
    rt_uint8_t mediaAccessPriority;
    rt_uint8_t randomSelectInterval;
    rt_uint8_t racMsgAccessLimit;
    rt_uint8_t networkStatus;
    rt_uint8_t arqTimeoutLimit;
    rt_uint16_t announcementVersion;
} ASC_MAC;

typedef struct {
    uint16_t start_slot;
    uint16_t end_slot;
    void (*task_func)(void);
    uint8_t executed;
    uint8_t RT_state;
    uint32_t rx_message_count;
    uint8_t used_flag;
    uint8_t slot_lenth;
} TimeSegment;

typedef struct {
    uint16_t linkID;
    rt_uint8_t direction;
    rt_uint8_t timeslots;
    rt_uint8_t phy_channel;
    rt_uint8_t phy_channel_res;
    rt_int32_t fecBytes;
    double es_n0;
} LinkIDInfo;

typedef struct {
    rt_uint8_t sat_ID;
    rt_uint32_t ship_id;
    rt_uint32_t dest_id;
    rt_uint8_t send_pri;
    uint16_t link_id;
    rt_uint8_t message[1024 * 1024];
    rt_int32_t lenth;
    rt_int32_t lenth_2send;
    rt_int32_t send_state;
    rt_int32_t state;
    uint8_t downlink_cqi;
    uint8_t talk_id;
    rt_uint32_t request_send_state;
    rt_uint32_t request_ack_state;
    uint16_t slot_num;
    uint16_t slot_num_end;
    uint8_t sub_frame;
    uint8_t phy_ch;
    rt_uint32_t request_timeout;
} Modu_2send;

typedef struct {
    uint8_t type;
    uint8_t ship_id[4];
    uint8_t sat_id;
    uint8_t priority_size;
    uint8_t terminal_cap;
    uint8_t downlink_cqi;
    uint8_t reserved;
} ResourceRequest;

typedef struct {
    rt_uint8_t type;
    rt_uint8_t stationID[4];
    rt_uint8_t satID;
    rt_uint8_t destID[4];
    rt_uint8_t sessionID;
} RAC_EndOfTransmit;

#pragma pack(pop)

struct LogicalChannelConfig {
    uint16_t dlFreq;
    uint16_t ulFreq;
    uint8_t bandwidth;
    uint8_t slotSizes[12];
    uint8_t functions[12];
};

struct SatelliteBulletinBoard {
    uint8_t satelliteID;
    uint8_t mainNetID;
    uint8_t roamNetID;
    uint16_t sbbVersion;
    uint32_t startTime;
    uint16_t availabilityPeriod;
    uint8_t serviceCapability;
    uint16_t spareFreq;
    uint16_t uplinkMaxLength;
    uint8_t reserved;
    uint16_t totalLength;
    LogicalChannelConfig channels[6];
    std::vector<uint8_t> digitalSignature1;
    std::vector<uint8_t> digitalSignature2;
    SatelliteBulletinBoard() { memset(this, 0, sizeof(*this)); }
};

extern Modu_2send_short Modu_data_Tx_noack[MES_BUF_MAX];
extern Modu_2send_short Modu_data_Tx_noack_id[MES_BUF_MAX];
extern GL_event_flag GL_event_control;
extern ASC_MAC ASC_MAC_msg;
extern TimeSegment minute_schedule[SEGMENT_COUNT];
extern rt_uint8_t slot_occupy[10];
extern rt_int16_t start_slot[SEGMENT_COUNT];
extern rt_int16_t stop_slot[SEGMENT_COUNT];
extern const LinkIDInfo linkID_Table[LINK_ID_NUM];
extern SatelliteBulletinBoard g_currentSBB;

extern socket_t g_udpSocket;
extern std::string g_boardIp;
extern uint16_t g_boardPort;

void vdes_global_init();
rt_uint8_t Bytesum_check(rt_uint8_t* buf, rt_uint32_t len);
rt_uint16_t generate_rac_offset(void);
rt_uint32_t sum_slot(rt_uint8_t* src, rt_uint32_t seg);
void slot_resize(void);
rt_uint32_t rac_pack(uint8_t* buffer, rt_uint32_t len, int currentSlot);
void Down_frame_pack(rt_int16_t start_slot_num, uint8_t linkid_num,
    rt_uint8_t* buf, rt_uint32_t lenth, rt_uint32_t conv, rt_uint8_t ch);
int modu_data_pack_send(uint8_t* p08, uint8_t linkid, uint16_t number_slot,
    int32_t length, uint8_t* buffer, uint8_t conv, uint8_t ch);
void sendToBoard(uint8_t phyChannel, uint16_t lcStartSlot, uint8_t linkId,
    const uint8_t* rawData, int rawLen, const std::string& source,
    int16_t freqOffset = 0, uint16_t attenuation = 1);
std::string buildBoardFrameString(uint8_t phyChannel, uint16_t lcStartSlot, uint8_t linkId,
    const uint8_t* rawData, int rawLen);

#endif