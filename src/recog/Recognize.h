#pragma once

#include "log/Log.h"
#include "ini/IniParser.h"
#include <memory>
#include <mutex>

struct demo_recog_channel_t;

#define RECOGNIZE_TYPE_TENCENT "tencent"

class Recognize {
public:
    enum RecognizeType {
        NONE,
        TENCENT
    };
    static std::shared_ptr<Recognize> Create(string channelId);

    virtual ~Recognize() = default;
    void setPartial(bool val);
    void setRecogChannel(demo_recog_channel_t* val);
    string getVoiceId();

    virtual int init() = 0;
    virtual void stop() = 0;
    virtual int write(char* buff, int len) = 0;
    void sendStartOfInput();
    void sendComplete(string text);

    static string GetVoiceId(string channelId);
    static std::shared_ptr<Recognize> GetRecognize(string voiceId);
    static void Del(string channelId, string voiceId);
    static void Del(string channelId);
    static void Set(string channelId, std::shared_ptr<Recognize> val);

protected:
    void loadConfig();

protected:
    static string sConfigFile;

    demo_recog_channel_t* mRecogChannel = nullptr;
    string mChannelId;
    string mVoiceId;

    std::mutex mMutex;
    bool mIsStop = false;
    RecognizeType mRecognizeType = NONE;
    std::shared_ptr<IniParser> mIniParser;
    std::string mAppId;
    std::string mSecretId;
    std::string mSecretKey;
    bool mIsPartial = false;

    static std::mutex sMutex;
    static map<string, string> sChannelIdMap;
    static map<string, std::shared_ptr<Recognize>> sMap;
};
