#pragma once

#include "Synthesizer.h"
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <pthread.h>
#include <unistd.h>
#include "speech_synthesizer.h"
#include "tcloud_util.h"

class TencentSynthesizer : public Synthesizer {
public:
    ~TencentSynthesizer();
    virtual int init();
    virtual void stop();

private:
    std::unique_ptr<SpeechSynthesizer> mSpeechSynthesizer;
};
