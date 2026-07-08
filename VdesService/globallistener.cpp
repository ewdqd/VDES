// globallistener.cpp
#include "globallistener.h"
#include <cstring>
#include <iostream>
#include <map>
#include <algorithm>
using namespace std;

GlobalListener::GlobalListener(FragmentManager* fragmentMgr)
    : m_currentCollectingVersion(0), m_hasFragment1(false), m_fragmentMgr(fragmentMgr) {
}

GlobalListener::~GlobalListener() {}

void GlobalListener::handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) {
    if (packet.empty()) return;
    const uint8_t* data = packet.data();
    uint8_t type = data[0];
   // cout << "[GlobalListener] Received type " << (int)type << ", slot=" << slot << endl;

    switch (type) {
    case 1: parseSbbFragment1(data, packet.size()); break;
    case 2: parseSbbFragment2(data, packet.size()); break;
    case 3: parseSbbFragment3(data, packet.size()); break;
    case 4: parseSbbFragment4(data, packet.size()); break;
    case 5: parseSbbFragment5(data, packet.size()); break;
    case 6: parseSbbFragment6(data, packet.size()); break;
    case 10: parseMacFrame(data, packet.size()); break;
    case 11: parsePaging(data, packet.size()); break;
    case 12: parseResourceAllocation(data, packet.size()); break;
    case 30: parseStartFragment(data, packet.size()); break;
    case 31: parseContinueFragment(data, packet.size()); break;
    case 32: parseEndFragment(data, packet.size()); break;
    default: break;
    }
}

// ---------- SBB 片段1 ----------
void GlobalListener::parseSbbFragment1(const uint8_t* data, int len) {
    if (len < 20) return;
    SbbFragment frag;
    frag.fragmentNumber = data[0];
    frag.satelliteID = data[1];
    frag.mainNetID = data[2];
    frag.roamNetID = data[3];
    frag.version = (data[4] << 8) | data[5];
    frag.totalLength = (data[18] << 8) | data[19];
    frag.data.assign(data, data + len);

    if (frag.version > m_currentCollectingVersion) {
        m_sbbFragments.clear();
        m_currentCollectingVersion = frag.version;
        m_hasFragment1 = true;
        m_sbbFragments[1] = frag;
    }
    else if (frag.version == m_currentCollectingVersion && !m_sbbFragments.count(1)) {
        m_sbbFragments[1] = frag;
        m_hasFragment1 = true;
    }
}

// ---------- SBB 片段2 ----------
void GlobalListener::parseSbbFragment2(const uint8_t* data, int len) {
    if (len < 35) return;
    if (data[0] != 2) return;
    if (m_hasFragment1) {
        SbbFragment frag;
        frag.fragmentNumber = 2;
        frag.version = m_currentCollectingVersion;
        frag.data.assign(data, data + len);
        m_sbbFragments[2] = frag;
    }
}

// ---------- SBB 片段3 ----------
void GlobalListener::parseSbbFragment3(const uint8_t* data, int len) {
    if (len < 35) return;
    if (data[0] != 3) return;
    if (m_hasFragment1) {
        SbbFragment frag;
        frag.fragmentNumber = 3;
        frag.version = m_currentCollectingVersion;
        frag.data.assign(data, data + len);
        m_sbbFragments[3] = frag;
    }
}

// ---------- SBB 片段4 ----------
void GlobalListener::parseSbbFragment4(const uint8_t* data, int len) {
    if (len < 35) return;
    if (data[0] != 4) return;
    if (m_hasFragment1) {
        SbbFragment frag;
        frag.fragmentNumber = 4;
        frag.version = m_currentCollectingVersion;
        frag.data.assign(data, data + len);
        m_sbbFragments[4] = frag;
    }
}

// ---------- SBB 片段5 ----------
void GlobalListener::parseSbbFragment5(const uint8_t* data, int len) {
    if (len < 2) return;
    if (!m_hasFragment1) return;
    if (data[0] != 5) return;
    SbbFragment frag;
    frag.fragmentNumber = 5;
    frag.version = m_currentCollectingVersion;
    frag.data.assign(data, data + len);
    m_sbbFragments[5] = frag;
    if (m_sbbFragments.size() == 6) assembleSbb();
}

// ---------- SBB 片段6 ----------
void GlobalListener::parseSbbFragment6(const uint8_t* data, int len) {
    if (len < 2) return;
    if (!m_hasFragment1) return;
    if (data[0] != 6) return;
    SbbFragment frag;
    frag.fragmentNumber = 6;
    frag.version = m_currentCollectingVersion;
    frag.data.assign(data, data + len);
    m_sbbFragments[6] = frag;
    if (m_sbbFragments.size() == 6) assembleSbb();
}

// ---------- MAC 帧 ----------
void GlobalListener::parseMacFrame(const uint8_t* data, int len) {
    if (len < 13) return;
    ASC_MAC_msg.type = data[0];
    ASC_MAC_msg.payloadSize = (data[1] << 8) | data[2];
    ASC_MAC_msg.satID = data[3];
    ASC_MAC_msg.mainNetID = data[4];
    ASC_MAC_msg.roamNetID = data[5];
    ASC_MAC_msg.mediaAccessPriority = data[6];
    ASC_MAC_msg.randomSelectInterval = data[7];
    ASC_MAC_msg.racMsgAccessLimit = data[8];
    ASC_MAC_msg.networkStatus = data[9];
    ASC_MAC_msg.arqTimeoutLimit = data[10];
    ASC_MAC_msg.announcementVersion = (data[11] << 8) | data[12];
    GL_event_control.random_interval = ASC_MAC_msg.randomSelectInterval;
    if (macFrameUpdated) macFrameUpdated();
}

// ---------- 寻呼 ----------
void GlobalListener::parsePaging(const uint8_t* data, int len) {
    if (len < 3 || data[0] != 11) return;
    int numShips = (len - 3) / 4;
    for (int i = 0; i < numShips; ++i) {
        uint32_t shipId = (data[3 + 4 * i] << 24) | (data[4 + 4 * i] << 16) |
            (data[5 + 4 * i] << 8) | data[6 + 4 * i];
        if (pagingReceived) pagingReceived(shipId);
    }
}

// ---------- 资源分配 ----------
void GlobalListener::parseResourceAllocation(const uint8_t* data, int len) {
    if (len < 3 || data[0] != 12) return;
    uint16_t payloadSize = (data[1] << 8) | data[2];
    int remaining = len - 3;
    int numShips = remaining / 8;
    if (numShips > 4) numShips = 4;
    ResourceAllocationFragment frag;
    frag.type = data[0];
    frag.payloadSize = payloadSize;
    for (int i = 0; i < numShips; ++i) {
        int idx = 3 + i * 8;
        uint32_t stationID = (data[idx] << 24) | (data[idx + 1] << 16) |
            (data[idx + 2] << 8) | data[idx + 3];
        uint8_t logicChannel = data[idx + 4];
        uint8_t linkID = data[idx + 5];
        uint8_t sessionID = data[idx + 6];
        uint8_t uplinkCQI = data[idx + 7];
        switch (i) {
        case 0: frag.shipRadioId1 = stationID; frag.logicalChannel1 = logicChannel;
            frag.linkId1 = linkID; frag.sessionId1 = sessionID; frag.ulCqi1 = uplinkCQI; break;
        case 1: frag.shipRadioId2 = stationID; frag.logicalChannel2 = logicChannel;
            frag.linkId2 = linkID; frag.sessionId2 = sessionID; frag.ulCqi2 = uplinkCQI; break;
        case 2: frag.shipRadioId3 = stationID; frag.logicalChannel3 = logicChannel;
            frag.linkId3 = linkID; frag.sessionId3 = sessionID; frag.ulCqi3 = uplinkCQI; break;
        case 3: frag.shipRadioId4 = stationID; frag.logicalChannel4 = logicChannel;
            frag.linkId4 = linkID; frag.sessionId4 = sessionID; frag.ulCqi4 = uplinkCQI; break;
        }
        if (stationID == MY_MMSI && myResourceAllocationReceived)
            myResourceAllocationReceived(logicChannel, linkID, sessionID, uplinkCQI);
    }
    // 广播资源分配检测
    if (remaining >= 32) {
        uint32_t id1 = (data[3] << 24) | (data[4] << 16) | (data[5] << 8) | data[6];
        uint32_t id2 = (data[11] << 24) | (data[12] << 16) | (data[13] << 8) | data[14];
        uint32_t id3 = (data[19] << 24) | (data[20] << 16) | (data[21] << 8) | data[22];
        uint32_t id4 = (data[27] << 24) | (data[28] << 16) | (data[29] << 8) | data[30];
        if (id1 == 0 && id2 == 0 && id3 == 0 && id4 == 0 && broadcastResourceAllocationReceived)
        {
            cout << "[GlobalListener] Broadcast resource allocation detected" << endl;   // 新增
            if (broadcastResourceAllocationReceived) broadcastResourceAllocationReceived();
        }
    }
    if (m_fragmentMgr) m_fragmentMgr->setResourceAllocationFragment(frag);
}

// ---------- 起始片段 (type 30) ----------
void GlobalListener::parseStartFragment(const uint8_t* data, int len) {
    if (len < 15) return;
    BaseFragment frag;
    frag.type = data[0];
    frag.fieldSize = (data[1] << 8) | data[2];
    frag.srcRadioId = (data[3] << 24) | (data[4] << 16) | (data[5] << 8) | data[6];
    frag.satelliteId = data[7];
    frag.sessionId = data[8];
    frag.destRadioId = (data[9] << 24) | (data[10] << 16) | (data[11] << 8) | data[12];
    frag.fragmentNum = (data[13] << 8) | data[14];
    frag.payload.assign(data + 15, data + len);
    if (m_fragmentMgr) m_fragmentMgr->setStartFragment(frag);
}

// ---------- 延续片段 (type 31) ----------
void GlobalListener::parseContinueFragment(const uint8_t* data, int len) {
    if (len < 15) return;
    BaseFragment frag;
    frag.type = data[0];
    frag.fieldSize = (data[1] << 8) | data[2];
    frag.srcRadioId = (data[3] << 24) | (data[4] << 16) | (data[5] << 8) | data[6];
    frag.satelliteId = data[7];
    frag.sessionId = data[8];
    frag.destRadioId = (data[9] << 24) | (data[10] << 16) | (data[11] << 8) | data[12];
    frag.fragmentNum = (data[13] << 8) | data[14];
    frag.payload.assign(data + 15, data + len);
    if (m_fragmentMgr) m_fragmentMgr->addContinueFragment(frag);
}

// ---------- 结束片段 (type 32) ----------
void GlobalListener::parseEndFragment(const uint8_t* data, int len) {
    if (len < 15) return;
    BaseFragment frag;
    frag.type = data[0];
    frag.fieldSize = (data[1] << 8) | data[2];
    frag.srcRadioId = (data[3] << 24) | (data[4] << 16) | (data[5] << 8) | data[6];
    frag.satelliteId = data[7];
    frag.sessionId = data[8];
    frag.destRadioId = (data[9] << 24) | (data[10] << 16) | (data[11] << 8) | data[12];
    frag.fragmentNum = (data[13] << 8) | data[14];
    frag.payload.assign(data + 15, data + len);
    if (m_fragmentMgr) m_fragmentMgr->setEndFragment(frag);
}

// ---------- 组装完整 SBB ----------
void GlobalListener::assembleSbb() {
    SatelliteBulletinBoard newSBB;

    // ========== 1. 解析片段1：基本信息 ==========
    const uint8_t* f1 = m_sbbFragments[1].data.data();
    newSBB.satelliteID = f1[1];
    newSBB.mainNetID = f1[2];
    newSBB.roamNetID = f1[3];
    newSBB.sbbVersion = (f1[4] << 8) | f1[5];
    newSBB.startTime = (f1[6] << 24) | (f1[7] << 16) | (f1[8] << 8) | f1[9];
    newSBB.availabilityPeriod = (f1[10] << 8) | f1[11];
    newSBB.serviceCapability = f1[12];
    newSBB.spareFreq = (f1[13] << 8) | f1[14];
    newSBB.uplinkMaxLength = (f1[15] << 8) | f1[16];
    newSBB.reserved = f1[17];
    newSBB.totalLength = (f1[18] << 8) | f1[19];

    // ========== 2. 解析片段2：信道 A 和 B ==========
    if (m_sbbFragments.count(2)) {
        const uint8_t* f2 = m_sbbFragments[2].data.data();
        int idx = 1; // 跳过类型字节

        // 信道 A
        newSBB.channels[0].dlFreq = (f2[idx] << 8) | f2[idx + 1]; idx += 2;
        newSBB.channels[0].ulFreq = (f2[idx] << 8) | f2[idx + 1]; idx += 2;
        newSBB.channels[0].bandwidth = f2[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f2[idx++];
            newSBB.channels[0].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[0].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f2[idx++];
            newSBB.channels[0].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[0].functions[i * 2 + 1] = packed & 0x0F;
        }

        // 信道 B
        newSBB.channels[1].dlFreq = (f2[idx] << 8) | f2[idx + 1]; idx += 2;
        newSBB.channels[1].ulFreq = (f2[idx] << 8) | f2[idx + 1]; idx += 2;
        newSBB.channels[1].bandwidth = f2[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f2[idx++];
            newSBB.channels[1].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[1].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f2[idx++];
            newSBB.channels[1].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[1].functions[i * 2 + 1] = packed & 0x0F;
        }
    }

    // ========== 3. 解析片段3：信道 C 和 D ==========
    if (m_sbbFragments.count(3)) {
        const uint8_t* f3 = m_sbbFragments[3].data.data();
        int idx = 1;

        // 信道 C
        newSBB.channels[2].dlFreq = (f3[idx] << 8) | f3[idx + 1]; idx += 2;
        newSBB.channels[2].ulFreq = (f3[idx] << 8) | f3[idx + 1]; idx += 2;
        newSBB.channels[2].bandwidth = f3[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f3[idx++];
            newSBB.channels[2].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[2].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f3[idx++];
            newSBB.channels[2].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[2].functions[i * 2 + 1] = packed & 0x0F;
        }

        // 信道 D
        newSBB.channels[3].dlFreq = (f3[idx] << 8) | f3[idx + 1]; idx += 2;
        newSBB.channels[3].ulFreq = (f3[idx] << 8) | f3[idx + 1]; idx += 2;
        newSBB.channels[3].bandwidth = f3[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f3[idx++];
            newSBB.channels[3].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[3].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f3[idx++];
            newSBB.channels[3].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[3].functions[i * 2 + 1] = packed & 0x0F;
        }
    }

    // ========== 4. 解析片段4：信道 E 和 F ==========
    if (m_sbbFragments.count(4)) {
        const uint8_t* f4 = m_sbbFragments[4].data.data();
        int idx = 1;

        // 信道 E
        newSBB.channels[4].dlFreq = (f4[idx] << 8) | f4[idx + 1]; idx += 2;
        newSBB.channels[4].ulFreq = (f4[idx] << 8) | f4[idx + 1]; idx += 2;
        newSBB.channels[4].bandwidth = f4[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f4[idx++];
            newSBB.channels[4].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[4].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f4[idx++];
            newSBB.channels[4].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[4].functions[i * 2 + 1] = packed & 0x0F;
        }

        // 信道 F
        newSBB.channels[5].dlFreq = (f4[idx] << 8) | f4[idx + 1]; idx += 2;
        newSBB.channels[5].ulFreq = (f4[idx] << 8) | f4[idx + 1]; idx += 2;
        newSBB.channels[5].bandwidth = f4[idx]; idx++;
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f4[idx++];
            newSBB.channels[5].slotSizes[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[5].slotSizes[i * 2 + 1] = packed & 0x0F;
        }
        for (int i = 0; i < 6; i++) {
            uint8_t packed = f4[idx++];
            newSBB.channels[5].functions[i * 2] = (packed >> 4) & 0x0F;
            newSBB.channels[5].functions[i * 2 + 1] = packed & 0x0F;
        }
    }

    // ========== 5. 数字签名（片段5和6） ==========
    if (m_sbbFragments.count(5)) {
        const uint8_t* f5 = m_sbbFragments[5].data.data();
        int sigLen = m_sbbFragments[5].data.size() - 1;
        if (sigLen > 0) {
            newSBB.digitalSignature1.assign(f5 + 1, f5 + 1 + sigLen);
        }
    }
    if (m_sbbFragments.count(6)) {
        const uint8_t* f6 = m_sbbFragments[6].data.data();
        int sigLen = m_sbbFragments[6].data.size() - 1;
        if (sigLen > 0) {
            newSBB.digitalSignature2.assign(f6 + 1, f6 + 1 + sigLen);
        }
    }

    // 更新全局 SBB 并发射信号
    g_currentSBB = newSBB;
    if (sbbParsed) sbbParsed(newSBB);

    // 清理缓存
    m_sbbFragments.clear();
    m_hasFragment1 = false;
}