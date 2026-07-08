#include "vdes_global.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <iomanip>

Modu_2send_short Modu_data_Tx_noack[MES_BUF_MAX];
Modu_2send_short Modu_data_Tx_noack_id[MES_BUF_MAX];
rt_int8_t tx_time[MES_BUF_MAX];
rt_int8_t tx_time_ack[MES_BUF_MAX];
GL_event_flag GL_event_control;
ASC_MAC ASC_MAC_msg;
TimeSegment minute_schedule[SEGMENT_COUNT];
rt_uint8_t slot_occupy[10] = { 90, 30, 90, 90, 90, 90, 30, 30, 179, 1 };
rt_int16_t start_slot[SEGMENT_COUNT];
rt_int16_t stop_slot[SEGMENT_COUNT];
rt_uint32_t ASC_IN_slot[15];
SatelliteBulletinBoard g_currentSBB;

socket_t g_udpSocket = -1;
std::string g_boardIp = "127.0.0.1";
uint16_t g_boardPort = 8080;
int g_currentSlot = 0;

void setGlobalCurrentSlot(int slot) { g_currentSlot = slot; }
int getGlobalCurrentSlot() { return g_currentSlot; }

const LinkIDInfo linkID_Table[LINK_ID_NUM] = {
    {25, 0x5A, 90, LOGIC_CH_A, LOGIC_CH_B, 597, -2.0},
    {26, 0x5A, 90, LOGIC_CH_A, LOGIC_CH_B, 7 * 682, -2.4},
    {27, 0x5A, 90, LOGIC_CH_A, LOGIC_CH_B, 19 * 754, 5.0},
    {28, 0x5A, 90, LOGIC_CH_C, LOGIC_CH_D, 4 * 660, -2.0},
    {29, 0x5A, 90, LOGIC_CH_E, LOGIC_CH_F, 6 * 694, -2.0},
    {32, 0x5A, 15, LOGIC_CH_A, LOGIC_CH_B, 39, -4.5},
    {33, 0x5A, 15, LOGIC_CH_A, LOGIC_CH_B, 535, -3.6},
    {34, 0x5A, 15, LOGIC_CH_A, LOGIC_CH_B, 1040, -0.6},
    {20, 0xA5, 5,  LOGIC_CH_A, LOGIC_CH_B, 12, -0.9},
    {21, 0xA5, 1,  LOGIC_CH_A, LOGIC_CH_B, 92, 3.9},
    {22, 0xA5, 3,  LOGIC_CH_A, LOGIC_CH_B, 390, 3.9},
    {24, 0xA5, 3,  LOGIC_CH_A, LOGIC_CH_B, 947, 12.2}
};

void vdes_global_init() {
    memset(Modu_data_Tx_noack, 0, sizeof(Modu_data_Tx_noack));
    memset(Modu_data_Tx_noack_id, 0, sizeof(Modu_data_Tx_noack_id));
    memset(tx_time, 0, sizeof(tx_time));
    memset(tx_time_ack, 0, sizeof(tx_time_ack));
    memset(&GL_event_control, 0, sizeof(GL_event_control));
    memset(&ASC_MAC_msg, 0, sizeof(ASC_MAC_msg));
    memset(minute_schedule, 0, sizeof(minute_schedule));
    memset(ASC_IN_slot, 0, sizeof(ASC_IN_slot));
    memset(&g_currentSBB, 0, sizeof(g_currentSBB));
    slot_resize();
}

rt_uint8_t Bytesum_check(rt_uint8_t* buf, rt_uint32_t len) {
    rt_uint32_t sum = 0;
    for (rt_uint32_t i = 0; i < len; i++) sum += buf[i];
    return (rt_uint8_t)(sum & 0xFF);
}

rt_uint16_t generate_rac_offset(void) {
    return (rand() % NUM_VALUES) * RAC_STEP;
}

rt_uint32_t sum_slot(rt_uint8_t* src, rt_uint32_t seg) {
    rt_uint32_t result = 0, i = 0;
    if (seg == 0) return 0;
    seg -= 1;
    while (seg >= 10) {
        result += 720;
        seg -= 10;
    }
    if (seg > 0) {
        for (i = 0; i < seg; i++) result += src[i];
    }
    result += 90;
    return result;
}

void slot_resize(void) {
    for (int i = 0; i < SEGMENT_COUNT; i++) {
        start_slot[i] = sum_slot(slot_occupy, i);
        minute_schedule[i].start_slot = start_slot[i];
        stop_slot[i] = sum_slot(slot_occupy, i + 1);
        minute_schedule[i].end_slot = stop_slot[i];
        minute_schedule[i].slot_lenth = minute_schedule[i].end_slot - minute_schedule[i].start_slot;
        minute_schedule[i].used_flag = SEG_UNUSED;
    }
    for (int i = 0; i < 3; i++) {
        ASC_IN_slot[5 * i] = minute_schedule[10 * i + 1].start_slot + 15;
        ASC_IN_slot[5 * i + 1] = minute_schedule[10 * i + 1].start_slot + 30;
        ASC_IN_slot[5 * i + 2] = minute_schedule[10 * i + 1].start_slot + 45;
        ASC_IN_slot[5 * i + 3] = minute_schedule[10 * i + 1].start_slot + 60;
        ASC_IN_slot[5 * i + 4] = minute_schedule[10 * i + 1].start_slot + 75;
    }
}

// 上位机→板卡帧 (0xEBA0F9) 符合规格: 物理信道(1) + LC时隙(2) + 预留(1) + 预留(1) + LinkID(1) + 频率偏移(2) + 衰减(2) + 原始报文(N)
void sendToBoard(uint8_t phyChannel, uint16_t lcStartSlot, uint8_t linkId,
    const uint8_t* rawData, int rawLen, const std::string& source,
    int16_t freqOffset, uint16_t attenuation) {
    if (g_udpSocket == -1) return;
    static uint16_t frameSeq = 0;
    std::vector<uint8_t> frame;
    frame.push_back(0xEB);
    frame.push_back(0xA0);
    frame.push_back(0xF9);
    uint16_t seq = frameSeq++;
    frame.push_back(seq & 0xFF);
    frame.push_back((seq >> 8) & 0xFF);

    int payloadLen = 1 + 2 + 1 + 1 + 1 + 2 + 2 + rawLen;   // 10 + rawLen
    frame.push_back(payloadLen & 0xFF);
    frame.push_back((payloadLen >> 8) & 0xFF);
    frame.push_back(phyChannel);
    frame.push_back(lcStartSlot & 0xFF);
    frame.push_back((lcStartSlot >> 8) & 0xFF);
    frame.push_back(0x00);   // 预留
    frame.push_back(0x00);   // 预留
    frame.push_back(linkId);
    // 频率偏移 (有符号16位, 单位Hz)
    frame.push_back(freqOffset & 0xFF);
    frame.push_back((freqOffset >> 8) & 0xFF);
    // 衰减 (实际衰减值×4096取整, 默认1)
    frame.push_back(attenuation & 0xFF);
    frame.push_back((attenuation >> 8) & 0xFF);
    frame.insert(frame.end(), rawData, rawData + rawLen);
    uint8_t checksum = 0;
    for (size_t i = 3; i < frame.size(); ++i) checksum += frame[i];
    frame.push_back(checksum);
    send_udp(g_udpSocket, frame.data(), frame.size(), g_boardIp, g_boardPort);
}
// ========================================================================

int modu_data_pack_send(uint8_t* p08, uint8_t linkid, uint16_t number_slot,
    int32_t length, uint8_t* buffer, uint8_t conv, uint8_t ch) {
    static const uint8_t phyChannelMap[] = { PHY_CH_A, PHY_CH_B, PHY_CH_C, PHY_CH_D, PHY_CH_E, PHY_CH_F };
    uint8_t phyChannel = (ch < 6) ? phyChannelMap[ch] : PHY_CH_A;
    sendToBoard(phyChannel, number_slot, linkid, buffer, length, "modu_data_pack_send");
    return 0;
}

void Down_frame_pack(rt_int16_t start_slot_num, uint8_t linkid_num,
    rt_uint8_t* buf, rt_uint32_t lenth, rt_uint32_t conv, rt_uint8_t ch) {
    modu_data_pack_send(nullptr, linkid_num, start_slot_num, lenth, buf, conv, ch);
}

rt_uint32_t rac_pack(uint8_t* buffer, rt_uint32_t len, int currentSlot) {
    if (GL_event_control.rac_message_cnt < ASC_MAC_msg.racMsgAccessLimit) {
        rt_int16_t start_slot = currentSlot + 2;
        Down_frame_pack(start_slot, LINKID20, buffer, len, 0, LOGIC_CH_A);
        GL_event_control.rac_message_cnt++;
        return 0;
    }
    else {
        std::cerr << "rac_pack: RAC消息数量已达限制" << std::endl;
        return -1;
    }
}

std::string buildBoardFrameString(uint8_t phyChannel, uint16_t lcStartSlot, uint8_t linkId,
    const uint8_t* rawData, int rawLen) {
    std::vector<uint8_t> frame;
    frame.push_back(0xEB);
    frame.push_back(0xA0);
    frame.push_back(0xF9);
    static uint16_t displaySeq = 0;
    uint16_t seq = displaySeq++;
    frame.push_back(seq & 0xFF);
    frame.push_back((seq >> 8) & 0xFF);
    int payloadLen = 1 + 2 + 1 + 1 + 1 + 2 + 2 + rawLen;
    frame.push_back(payloadLen & 0xFF);
    frame.push_back((payloadLen >> 8) & 0xFF);
    frame.push_back(phyChannel);
    frame.push_back(lcStartSlot & 0xFF);
    frame.push_back((lcStartSlot >> 8) & 0xFF);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(linkId);
    frame.push_back(0x00);  // freqOffset低
    frame.push_back(0x00);  // freqOffset高
    frame.push_back(0x01);  // attenuation低
    frame.push_back(0x00);  // attenuation高
    frame.insert(frame.end(), rawData, rawData + rawLen);
    uint8_t checksum = 0;
    for (size_t i = 3; i < frame.size(); ++i) checksum += frame[i];
    frame.push_back(checksum);
    std::stringstream ss;
    for (uint8_t b : frame) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    return ss.str();
}