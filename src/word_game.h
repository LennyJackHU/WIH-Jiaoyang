#pragma once

#include "picker.h"
#include "wlp2_protocol.h"

class WordGame : public wlp2::Handler {
public:
    enum class View : uint8_t { Picker, Sentence };
    enum class Status : uint8_t {
        None,
        NoLink,
        TwoLinks,
        Sending,
        ReceivedLeft,
        ReceivedRight,
        Sent,
        SlotBusy,
        Invalid,
    };

    struct NeighborSnapshot {
        bool present = false;
        char id[17] = {};
        char boot[17] = {};
        char port[2] = {};
        char sid[33] = {};
    };

    struct RemoteSnapshot {
        bool valid = false;
        bool orderValid = false;
        char id[17] = {};
        char boot[17] = {};
        char text[wlp2::TEXT_MAX + 1] = {};
        uint32_t rev = 0;
        uint32_t topoRev = 0;
        NeighborSnapshot left;
        NeighborSnapshot right;
    };

    WordGame(wlp2::Node& node, PickerRandomFn pickerRandom, wlp2::RandomFn requestRandom);

    bool begin(const wlp2::Identity& identity, uint32_t now);
    void tick(uint32_t now);
    void pressConfirm(uint32_t now);
    void pressRefresh(uint32_t now);
    void turnEncoder(int steps, uint32_t now);

    void onLinkChanged(wlp2::Port port) override;
    void onNet(const wlp2::NetMessage& message, const wlp2::JsonReader& body) override;

    Picker& picker() { return picker_; }
    const Picker& picker() const { return picker_; }
    const char* currentWord() const { return picker_.currentWord(); }
    bool confirmed() const { return confirmed_; }
    View view() const { return view_; }
    Status status() const { return status_; }
    const char* statusText() const;
    const char* leftWord() const { return leftWord_; }
    const char* rightWord() const { return rightWord_; }
    bool hasSentence() const { return leftWord_[0] || rightWord_[0]; }
    uint32_t wordRev() const { return wordRev_; }
    uint32_t uiRevision() const { return uiRevision_; }
    bool transferPending() const { return pending_; }
    const char* pendingRequest() const { return pending_ ? pendingReq_ : ""; }
    uint32_t receivedTransfers() const { return receivedTransfers_; }
    uint8_t participantCount() const;
    const RemoteSnapshot& remote(wlp2::Port port) const;

    void restore(bool confirmed, const char* leftWord, const char* rightWord);
    bool consumeSaveRequest();
    bool consumeDisplayRequest();

private:
    struct Remote {
        RemoteSnapshot snapshot;
        uint32_t lastQuery;
        char queryReq[33];
    };

    struct Receipt {
        bool used = false;
        bool ackPending = false;
        char src[17] = {};
        char boot[17] = {};
        char req[33] = {};
        char text[wlp2::TEXT_MAX + 1] = {};
        wlp2::Port side = wlp2::Port::Left;
        uint32_t seenAt = 0;
    };

    void makeRequest(char* out);
    void changed();
    void setStatus(Status status);
    void clearSentence();
    void localWordChanged();
    bool directPeer(const char* id, wlp2::Port& port) const;
    bool directMessage(const wlp2::NetMessage& message) const;
    void tryOffer(uint32_t now);
    void receiveOffer(const wlp2::NetMessage& message, const wlp2::JsonReader& body);
    void receiveAccept(const wlp2::NetMessage& message, const wlp2::JsonReader& body);
    void sendError(const wlp2::NetMessage& message, const char* req, const char* code);
    void sendNodeInfo(const wlp2::NetMessage& message, const char* req);
    void writeNeighbor(wlp2::JsonWriter& out, const char* key, wlp2::Port port) const;
    void queryRemote(wlp2::Port port, uint32_t now);
    void receiveNodeInfo(const wlp2::NetMessage& message, const wlp2::JsonReader& body);
    void receiveState(const wlp2::NetMessage& message, const wlp2::JsonReader& body);
    bool readNeighbor(const wlp2::JsonReader& body, const char* key,
                      NeighborSnapshot& neighbor) const;
    void flushReceipts(uint32_t now);

    wlp2::Node& node_;
    wlp2::RandomFn requestRandom_;
    Picker picker_;
    bool confirmed_ = false;
    View view_ = View::Picker;
    Status status_ = Status::None;
    char leftWord_[wlp2::TEXT_MAX + 1] = {};
    char rightWord_[wlp2::TEXT_MAX + 1] = {};
    char leftSource_[17] = {};
    char rightSource_[17] = {};
    uint32_t wordRev_ = 0;
    uint32_t uiRevision_ = 0;
    uint32_t receivedTransfers_ = 0;
    uint32_t now_ = 0;
    uint32_t statusUntil_ = 0;
    bool saveRequested_ = false;
    bool displayRequested_ = false;
    Remote remotes_[2] = {};
    Receipt receipts_[8] = {};
    bool pending_ = false;
    char pendingReq_[33] = {};
    char pendingDst_[17] = {};
    char pendingText_[wlp2::TEXT_MAX + 1] = {};
    uint32_t nextOfferMs_ = 0;
};
