#ifndef GLOBALLISTENER_H
#define GLOBALLISTENER_H

#include <vector>
#include <cstdint>
#include <functional>
#include <map>          // 必须包含

#include "vdes_global.h"
#include "fragmentmanager.h"

class GlobalListener {
public:
    explicit GlobalListener(FragmentManager* fragmentMgr);
    ~GlobalListener();

    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel);

    // 回调
    std::function<void(const SatelliteBulletinBoard&)> sbbParsed;
    std::function<void()> macFrameUpdated;
    std::function<void(uint32_t)> pagingReceived;
    std::function<void(uint8_t, uint8_t, uint8_t, uint8_t)> myResourceAllocationReceived;
    std::function<void()> broadcastResourceAllocationReceived;

private:
    void parseSbbFragment1(const uint8_t* data, int len);
    void parseSbbFragment2(const uint8_t* data, int len);
    void parseSbbFragment3(const uint8_t* data, int len);
    void parseSbbFragment4(const uint8_t* data, int len);
    void parseSbbFragment5(const uint8_t* data, int len);
    void parseSbbFragment6(const uint8_t* data, int len);
    void parseMacFrame(const uint8_t* data, int len);
    void parsePaging(const uint8_t* data, int len);
    void parseResourceAllocation(const uint8_t* data, int len);
    void parseStartFragment(const uint8_t* data, int len);
    void parseContinueFragment(const uint8_t* data, int len);
    void parseEndFragment(const uint8_t* data, int len);
    void assembleSbb();

    struct SbbFragment {
        uint8_t fragmentNumber;
        std::vector<uint8_t> data;
        uint16_t version;
        uint8_t satelliteID;
        uint8_t mainNetID;
        uint8_t roamNetID;
        uint16_t totalLength;
    };
    std::map<int, SbbFragment> m_sbbFragments;
    uint16_t m_currentCollectingVersion;
    bool m_hasFragment1;
    FragmentManager* m_fragmentMgr;
};

#endif