#pragma once

#include "Recognize.h"

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
#include "speech_recognizer.h"
#include "tcloud_util.h"

class TencentRecognize : public Recognize {
public:
    ~TencentRecognize();
    virtual int init();
    virtual void stop();
    virtual int write(char* buff, int len);

private:
    std::unique_ptr<SpeechRecognizer> mSpeechRecognizer;
};
