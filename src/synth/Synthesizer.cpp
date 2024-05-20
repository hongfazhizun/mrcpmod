#include "Synthesizer.h"
#include "SynthEngine.h"
#include "TencentSynthesizer.h"
#include <mutex>

string Synthesizer::sConfigFile = "conf/config.ini";

std::shared_ptr<Synthesizer> Synthesizer::Create(string channelId)
{
    string type;
    auto ini = std::make_shared<IniParser>();
    ini->setFileName(sConfigFile);
    ini->get("generic", "type", type);
    if (SYNTHESIZER_TYPE_TENCENT == type) {
        INFOLN("create tencent synthesizer, channelId:%s", channelId.c_str());
        auto synthesizer = std::make_shared<TencentSynthesizer>();
        synthesizer->mChannelId = channelId;
        synthesizer->mSynthesizerType = TENCENT;
        synthesizer->mIniParser = ini;
        // gen unique voice_id
        boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
        synthesizer->mVoiceId = boost::uuids::to_string(a_uuid);
        return synthesizer;
    }
    INFOLN("synthesizer type is not support, type:%s channelId:%s", type.c_str(), channelId.c_str());
    return nullptr;
}

void Synthesizer::setSynthChannel(demo_synth_channel_t* val)
{
    mSynthChannel = val;
}

void Synthesizer::setVoiceName(string val)
{
    mVoiceName = val;
}

void Synthesizer::setText(string val)
{
    mText = val;
}

string Synthesizer::getVoiceId()
{
    return mVoiceId;
}

void Synthesizer::loadConfig()
{
    string type;
    mIniParser->get("generic", "type", type);
    mIniParser->get(type, "appid", mAppId);
    mIniParser->get(type, "secretid", mSecretId);
    mIniParser->get(type, "secretkey", mSecretKey);
}

std::mutex Synthesizer::sMutex;
map<string, string> Synthesizer::sChannelIdMap;
map<string, std::shared_ptr<Synthesizer>> Synthesizer::sMap;

string Synthesizer::GetVoiceId(string channelId)
{
    std::lock_guard<std::mutex> l(sMutex);
    auto it = sChannelIdMap.find(channelId);
    if (it == sChannelIdMap.end()) {
        return "";
    }
    return it->second;
}

std::shared_ptr<Synthesizer> Synthesizer::GetSynthesizer(string voiceId)
{
    std::lock_guard<std::mutex> l(sMutex);
    auto it = sMap.find(voiceId);
    if (it == sMap.end()) {
        return nullptr;
    }
    return it->second;
}

void Synthesizer::Del(string channelId, string voiceId)
{
    std::lock_guard<std::mutex> l(sMutex);
    sChannelIdMap.erase(channelId);
    sMap.erase(voiceId);
}

void Synthesizer::Del(string channelId)
{
    string voiceId = Synthesizer::GetVoiceId(channelId);
    if (voiceId.empty()) {
        WARNLN("voiceId is empty, channelId:%s", channelId.c_str());
        return;
    }
    auto synthesizer = Synthesizer::GetSynthesizer(voiceId);
    if (!synthesizer) {
        WARNLN("synthesizer is nullptr, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
        Synthesizer::Del(channelId, voiceId);
        return;
    }
    synthesizer->stop();
    Synthesizer::Del(channelId, voiceId);
    INFOLN("delete synthesizer, channelId:%s voiceId:%s", channelId.c_str(), voiceId.c_str());
}

void Synthesizer::Set(string channelId, std::shared_ptr<Synthesizer> val)
{
    std::lock_guard<std::mutex> l(sMutex);
    sChannelIdMap.erase(channelId);
    sChannelIdMap.emplace(channelId, val->mVoiceId);
    sMap.erase(val->mVoiceId);
    sMap.emplace(val->mVoiceId, val);
}

int Synthesizer::read(char* buff, int size)
{
    memset(buff, 0, size);
    std::unique_lock<std::mutex> l(mMutex);
    mCv.wait(l, [this, size] { return mIsStop || mIsEnd || mAudioData.size() >= size; });
    if (mAudioData.size() < size) {
        INFOLN("audio data is not enough, audio_data:%d size:%d voiceId:%s", mAudioData.size(), size, mVoiceId.c_str());
        size = mAudioData.size();
    }
    // INFOLN("audio data size, audio_data:%d size:%d voiceId:%s", mAudioData.size(), size, mVoiceId.c_str());
    if (size <= 0) {
        return -1;
    }
    std::vector<char> vec(mAudioData.begin(), mAudioData.begin() + size);
    memcpy(buff, vec.data(), size);
    mAudioData.erase(mAudioData.begin(), mAudioData.begin() + size);
    return 0;
}

void Synthesizer::pushData(char* data, int len)
{
    std::unique_lock<std::mutex> l(mMutex);
    if (mIsStop || mIsEnd) {
        return;
    }
    mAudioData.insert(mAudioData.end(), data, data + len);
    mCv.notify_all();
}

void Synthesizer::onSynthesisEnd()
{
    std::unique_lock<std::mutex> l(mMutex);
    mIsEnd = true;
    mAudioData.insert(mAudioData.end(), 160 * 5, 0);
    mCv.notify_all();
}
