#pragma once

#include "log/Log.h"
#include "ini/IniParser.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include <deque>

struct demo_synth_channel_t;

#define SYNTHESIZER_TYPE_TENCENT "tencent"

class Synthesizer {
public:
    enum SynthesizerType {
        NONE,
        TENCENT
    };

    static std::shared_ptr<Synthesizer> Create(string channelId);

    virtual ~Synthesizer() = default;
    void setSynthChannel(demo_synth_channel_t* val);
    void setVoiceName(string val);
    void setText(string val);
    string getVoiceId();

    virtual int init() = 0;
    virtual void stop() = 0;

    virtual int read(char* buff, int size);
    virtual void pushData(char* data, int len);
    virtual void onSynthesisEnd();

    static string GetVoiceId(string channelId);
    static std::shared_ptr<Synthesizer> GetSynthesizer(string voiceId);
    static void Del(string channelId, string voiceId);
    static void Del(string channelId);
    static void Set(string channelId, std::shared_ptr<Synthesizer> val);

protected:
    void loadConfig();

protected:
    static string sConfigFile;

    demo_synth_channel_t* mSynthChannel = nullptr;
    string mChannelId;
    string mVoiceId;
    string mVoiceName;
    string mText;

    std::mutex mMutex;
    std::condition_variable mCv;
    bool mIsStop = false;
    bool mIsEnd = false;
    std::deque<char> mAudioData;
    SynthesizerType mSynthesizerType = NONE;
    std::shared_ptr<IniParser> mIniParser;
    std::string mAppId;
    std::string mSecretId;
    std::string mSecretKey;

    static std::mutex sMutex;
    static map<string, string> sChannelIdMap;
    static map<string, std::shared_ptr<Synthesizer>> sMap;
};
