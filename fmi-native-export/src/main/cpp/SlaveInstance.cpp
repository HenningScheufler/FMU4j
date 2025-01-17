#include <fmu4j/SlaveInstance.hpp>

#include <fmu4j/jni_helper.hpp>
#include <cppfmu/cppfmu_cs.hpp>

#include <fstream>
#include <iostream>
#include <jni.h>
#include <string>
#include <utility>

namespace fmu4j
{

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4267) //conversion from 'size_t' to 'jsize', possible loss of data
#endif

SlaveInstance::SlaveInstance(
    JNIEnv* env,
    std::string instanceName,
    std::string resources)
    : resources_(std::move(resources))
    , instanceName_(std::move(instanceName))
{
    env->GetJavaVM(&jvm_);

    std::ifstream infile(resources_ + "/mainclass.txt");
    std::getline(infile, slaveName_);

    std::string classpath(resources_ + "/model.jar");
    classLoader_ = env->NewGlobalRef(create_classloader(env, classpath));

    jclass slaveCls = FindClass(env, classLoader_, slaveName_);
    if (slaveCls == nullptr) {
        std::string msg = "[FMU4j native] Unable to find class '" + slaveName_ + "'!";
        throw cppfmu::FatalError(msg.c_str());
    }

    ctorId_ = env->GetMethodID(slaveCls, "<init>", "(Ljava/util/Map;)V");
    if (ctorId_ == nullptr) {
        std::string msg =
            "Unable to locate 1 arg constructor that takes a Map for slave class '" + slaveName_ + "'!";
        throw cppfmu::FatalError(msg.c_str());
    }

    setupExperimentId_ = GetMethodID(env, slaveCls, "setupExperiment", "(DDD)V");
    enterInitialisationModeId_ = GetMethodID(env, slaveCls, "enterInitialisationMode", "()V");
    exitInitializationModeId_ = GetMethodID(env, slaveCls, "exitInitialisationMode", "()V");

    doStepId_ = GetMethodID(env, slaveCls, "doStep", "(DD)V");
    terminateId_ = GetMethodID(env, slaveCls, "terminate", "()V");
    closeId_ = GetMethodID(env, slaveCls, "close", "()V");

    getRealId_ = GetMethodID(env, slaveCls, "getReal", "([J)[D");
    setRealId_ = GetMethodID(env, slaveCls, "setReal", "([J[D)V");

    getIntegerId_ = GetMethodID(env, slaveCls, "getInteger", "([J)[I");
    setIntegerId_ = GetMethodID(env, slaveCls, "setInteger", "([J[I)V");

    getBooleanId_ = GetMethodID(env, slaveCls, "getBoolean", "([J)[Z");
    setBooleanId_ = GetMethodID(env, slaveCls, "setBoolean", "([J[Z)V");

    getStringId_ = GetMethodID(env, slaveCls, "getString", "([J)[Ljava/lang/String;");
    setStringId_ = GetMethodID(env, slaveCls, "setString", "([J[Ljava/lang/String;)V");

    jclass bulkCls = FindClass(env, classLoader_, "no.ntnu.ais.fmu4j.export.BulkRead", false);
    if (bulkCls != nullptr) {
        canGetSetAll_ = true;
        bulkIntValuesId_ = GetMethodID(env, bulkCls, "getIntValues", "()[I");
        bulkRealValuesId_ = GetMethodID(env, bulkCls, "getRealValues", "()[D");
        bulkBoolValuesId_ = GetMethodID(env, bulkCls, "getBoolValues", "()[Z");
        bulkStrValuesId_ = GetMethodID(env, bulkCls, "getStrValues", "()[Ljava/lang/String;");
        getAllId_ = GetMethodID(env, slaveCls, "getAll", "([J[J[J[J)Lno/ntnu/ais/fmu4j/export/BulkRead;");
        setAllId_ = GetMethodID(env, slaveCls, "setAll", "([J[I[J[D[J[Z[J[Ljava/lang/String;)V");
    }

    initialize();
}

void SlaveInstance::initialize()
{
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        env->DeleteGlobalRef(slaveInstance_);

        jclass slaveCls = FindClass(env, classLoader_, slaveName_);

        jclass mapCls = env->FindClass("java/util/HashMap");
        jmethodID mapCtor = GetMethodID(env, mapCls, "<init>", "()V");
        jmethodID putId = GetMethodID(env, mapCls, "put",
            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
        jobject map = env->NewObject(mapCls, mapCtor);
        env->CallObjectMethod(map, putId, env->NewStringUTF("instanceName"),
            env->NewStringUTF(instanceName_.c_str()));
        env->CallObjectMethod(map, putId, env->NewStringUTF("resourceLocation"),
            env->NewStringUTF(resources_.c_str()));

        slaveInstance_ = env->NewGlobalRef(env->NewObject(slaveCls, ctorId_, map));
        if (slaveInstance_ == nullptr) {
            std::string msg = "Unable to instantiate a new instance of '" + slaveName_ + "'!";
            throw cppfmu::FatalError(msg.c_str());
        }

        jmethodID defineId = GetMethodID(env, slaveCls, "__define__", "()V");
        env->CallObjectMethod(slaveInstance_, defineId);
    });
}

void SlaveInstance::SetupExperiment(cppfmu::FMIBoolean toleranceDefined, cppfmu::FMIReal tolerance,
    cppfmu::FMIReal tStart, cppfmu::FMIBoolean stopTimeDefined,
    cppfmu::FMIReal tStop)
{
    double stop = stopTimeDefined ? tStop : -1;
    double tol = toleranceDefined ? tolerance : -1;
    jvm_invoke(jvm_, [this, tStart, stop, tol](JNIEnv* env) {
        env->CallVoidMethod(slaveInstance_, setupExperimentId_, tStart, stop, tol);
    });
}

void SlaveInstance::EnterInitializationMode()
{
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        env->CallVoidMethod(slaveInstance_, enterInitialisationModeId_);
    });
}

void SlaveInstance::ExitInitializationMode()
{
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        env->CallVoidMethod(slaveInstance_, exitInitializationModeId_);
    });
}

bool SlaveInstance::DoStep(cppfmu::FMIReal currentCommunicationPoint, cppfmu::FMIReal communicationStepSize,
    cppfmu::FMIBoolean, cppfmu::FMIReal& endOfStep)
{
    bool status = true;
    jvm_invoke(jvm_, [this, &status, currentCommunicationPoint, communicationStepSize](JNIEnv* env) {
        env->CallVoidMethod(slaveInstance_, doStepId_, currentCommunicationPoint, communicationStepSize);
        if (env->ExceptionCheck()) {
            status = false;
        }
    });
    return status;
}

void SlaveInstance::Reset()
{
    onClose();
    initialize();
}

void SlaveInstance::Terminate()
{
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        env->CallBooleanMethod(slaveInstance_, terminateId_);
    });
}

void SlaveInstance::SetInteger(const cppfmu::FMIValueReference* vr, std::size_t nvr, const cppfmu::FMIInteger* value)
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));

        auto valueArray = env->NewIntArray(nvr);
        auto valueArrayElements = reinterpret_cast<jint*>(malloc(sizeof(jint) * nvr));

        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
            valueArrayElements[i] = static_cast<jint>(value[i]);
        }

        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);
        env->SetIntArrayRegion(valueArray, 0, nvr, valueArrayElements);

        env->CallVoidMethod(slaveInstance_, setIntegerId_, vrArray, valueArray);

        free(vrArrayElements);
        free(valueArrayElements);
    });
}

void SlaveInstance::SetReal(const cppfmu::FMIValueReference* vr, std::size_t nvr, const cppfmu::FMIReal* value)
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));

        auto valueArray = env->NewDoubleArray(nvr);
        auto valueArrayElements = reinterpret_cast<jdouble*>(malloc(sizeof(jdouble) * nvr));

        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
            valueArrayElements[i] = value[i];
        }

        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);
        env->SetDoubleArrayRegion(valueArray, 0, nvr, valueArrayElements);

        env->CallVoidMethod(slaveInstance_, setRealId_, vrArray, valueArray);

        free(vrArrayElements);
        free(valueArrayElements);
    });
}

void SlaveInstance::SetBoolean(const cppfmu::FMIValueReference* vr, std::size_t nvr, const cppfmu::FMIBoolean* value)
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));

        auto valueArray = env->NewBooleanArray(nvr);
        auto valueArrayElements = reinterpret_cast<jboolean*>(malloc(sizeof(jboolean) * nvr));

        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
            valueArrayElements[i] = static_cast<jboolean>(value[i]);
        }

        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);
        env->SetBooleanArrayRegion(valueArray, 0, nvr, valueArrayElements);

        env->CallVoidMethod(slaveInstance_, setBooleanId_, vrArray, valueArray);

        free(vrArrayElements);
        free(valueArrayElements);
    });
}

void SlaveInstance::SetString(const cppfmu::FMIValueReference* vr, std::size_t nvr, cppfmu::FMIString const* value)
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        clearStrBuffer(env);

        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));

        auto valueArray = env->NewObjectArray(nvr, env->FindClass("java/lang/String"), nullptr);

        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);

            const char* cStr = value[i];
            jstring jStr = env->NewStringUTF(cStr);
            jstring_ref ref{
                cStr = cStr,
                jStr = jStr};
            strBuffer.push_back(ref);

            env->SetObjectArrayElement(valueArray, i, jStr);
        }

        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);

        env->CallVoidMethod(slaveInstance_, setStringId_, vrArray, valueArray);

        free(vrArrayElements);
    });
}

void SlaveInstance::SetAll(
    const cppfmu::FMIValueReference* intVr, std::size_t nIntvr, const cppfmu::FMIInteger* intValue,
    const cppfmu::FMIValueReference* realVr, std::size_t nRealvr, const cppfmu::FMIReal* realValue,
    const cppfmu::FMIValueReference* boolVr, std::size_t nBoolvr, const cppfmu::FMIBoolean* boolValue,
    const cppfmu::FMIValueReference* strVr, std::size_t nStrvr, const cppfmu::FMIString* strValue)
{
    jvm_invoke(jvm_, [this, intVr, nIntvr, intValue, realVr, nRealvr, realValue, boolVr, nBoolvr, boolValue, strVr, nStrvr, strValue](JNIEnv* env) {
        if (canGetSetAll_) {
            auto intVrArray = env->NewLongArray(nIntvr);
            auto realVrArray = env->NewLongArray(nIntvr);
            auto boolVrArray = env->NewLongArray(nIntvr);
            auto strVrArray = env->NewLongArray(nStrvr);

            auto intValueArray = env->NewIntArray(nIntvr);
            auto realValueArray = env->NewDoubleArray(nRealvr);
            auto boolValueArray = env->NewBooleanArray(nBoolvr);
            auto strValueArray = env->NewObjectArray(nStrvr, env->FindClass("java/lang/String"), nullptr);

            auto intValueArrayElements = reinterpret_cast<jint*>(malloc(sizeof(jint) * nIntvr));
            auto intVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nIntvr));
            for (auto i = 0; i < nIntvr; i++) {
                intVrArrayElements[i] = static_cast<jlong>(intVr[i]);
                intValueArrayElements[i] = static_cast<jint>(intValue[i]);
            }
            env->SetLongArrayRegion(intVrArray, 0, nIntvr, intVrArrayElements);
            env->SetIntArrayRegion(intValueArray, 0, nIntvr, intValueArrayElements);

            auto realValueArrayElements = reinterpret_cast<jdouble*>(malloc(sizeof(jdouble) * nRealvr));
            auto realVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nRealvr));
            for (auto i = 0; i < nRealvr; i++) {
                realVrArrayElements[i] = static_cast<jlong>(realVr[i]);
                realValueArrayElements[i] = realValue[i];
            }
            env->SetLongArrayRegion(realVrArray, 0, nRealvr, realVrArrayElements);
            env->SetDoubleArrayRegion(realValueArray, 0, nRealvr, realValueArrayElements);

            auto boolValueArrayElements = reinterpret_cast<jboolean*>(malloc(sizeof(jboolean) * nBoolvr));
            auto boolVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nBoolvr));
            for (auto i = 0; i < nBoolvr; i++) {
                boolVrArrayElements[i] = static_cast<jlong>(boolVr[i]);
                boolValueArrayElements[i] = static_cast<jboolean>(boolValue[i]);
            }
            env->SetLongArrayRegion(boolVrArray, 0, nBoolvr, boolVrArrayElements);
            env->SetBooleanArrayRegion(boolValueArray, 0, nBoolvr, boolValueArrayElements);

            clearStrBuffer(env);
            auto strVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nStrvr));
            for (auto i = 0; i < nStrvr; i++) {
                strVrArrayElements[i] = static_cast<jlong>(strVr[i]);

                const char* cStr = strValue[i];
                jstring jStr = env->NewStringUTF(cStr);
                jstring_ref ref{
                    cStr = cStr,
                    jStr = jStr};
                strBuffer.push_back(ref);

                env->SetObjectArrayElement(strValueArray, i, jStr);
            }
            env->SetLongArrayRegion(strVrArray, 0, nStrvr, strVrArrayElements);

            env->CallVoidMethod(slaveInstance_, setAllId_,
                                intVrArray, intValueArray,
                                realVrArray, realValueArray,
                                boolVrArray, boolValueArray,
                                strVrArray, strValueArray);

            free(intVrArrayElements);
            free(realVrArrayElements);
            free(boolVrArrayElements);
            free(strVrArrayElements);
        } else {
            SetInteger(intVr, nIntvr, intValue);
            SetReal(realVr, nRealvr, realValue);
            SetBoolean(boolVr, nBoolvr, boolValue);
            SetString(strVr, nStrvr, strValue);
        }
    });
}

void SlaveInstance::GetInteger(const cppfmu::FMIValueReference* vr, std::size_t nvr, cppfmu::FMIInteger* value) const
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));
        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
        }
        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);

        auto valueArray = reinterpret_cast<jintArray>(env->CallObjectMethod(slaveInstance_, getIntegerId_, vrArray));
        auto valueArrayElements = env->GetIntArrayElements(valueArray, nullptr);

        for (int i = 0; i < nvr; i++) {
            value[i] = static_cast<fmi2Integer>(valueArrayElements[i]);
        }

        free(vrArrayElements);
        env->ReleaseIntArrayElements(valueArray, valueArrayElements, 0);
    });
}

void SlaveInstance::GetReal(const cppfmu::FMIValueReference* vr, std::size_t nvr, cppfmu::FMIReal* value) const
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));
        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
        }
        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);

        auto valueArray = reinterpret_cast<jdoubleArray>(env->CallObjectMethod(slaveInstance_, getRealId_, vrArray));
        auto valueArrayElements = env->GetDoubleArrayElements(valueArray, nullptr);

        for (int i = 0; i < nvr; i++) {
            value[i] = valueArrayElements[i];
        }

        free(vrArrayElements);
        env->ReleaseDoubleArrayElements(valueArray, valueArrayElements, 0);
    });
}

void SlaveInstance::GetBoolean(const cppfmu::FMIValueReference* vr, std::size_t nvr, cppfmu::FMIBoolean* value) const
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));
        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
        }
        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);

        auto valueArray = reinterpret_cast<jbooleanArray>(env->CallObjectMethod(slaveInstance_, getBooleanId_, vrArray));
        auto valueArrayElements = env->GetBooleanArrayElements(valueArray, nullptr);

        for (int i = 0; i < nvr; i++) {
            value[i] = static_cast<fmi2Boolean>(valueArrayElements[i]);
        }

        free(vrArrayElements);
        env->ReleaseBooleanArrayElements(valueArray, valueArrayElements, 0);
    });
}

void SlaveInstance::GetString(const cppfmu::FMIValueReference* vr, std::size_t nvr, cppfmu::FMIString* value) const
{
    jvm_invoke(jvm_, [this, vr, nvr, value](JNIEnv* env) {
        clearStrBuffer(env);

        auto vrArray = env->NewLongArray(nvr);
        auto vrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nvr));
        for (int i = 0; i < nvr; i++) {
            vrArrayElements[i] = static_cast<jlong>(vr[i]);
        }
        env->SetLongArrayRegion(vrArray, 0, nvr, vrArrayElements);

        auto valueArray = reinterpret_cast<jobjectArray>(env->CallObjectMethod(slaveInstance_, getStringId_, vrArray));
        for (int i = 0; i < nvr; i++) {
            auto jStr = reinterpret_cast<jstring>(env->GetObjectArrayElement(valueArray, i));
            auto cStr = env->GetStringUTFChars(jStr, nullptr);
            value[i] = cStr;
            jstring_ref ref{
                cStr = cStr,
                jStr = jStr};
            strBuffer.push_back(ref);
        }

        free(vrArrayElements);
    });
}

void SlaveInstance::GetAll(
    const cppfmu::FMIValueReference* intVr, std::size_t nIntvr, cppfmu::FMIInteger* intValue,
    const cppfmu::FMIValueReference* realVr, std::size_t nRealvr, cppfmu::FMIReal* realValue,
    const cppfmu::FMIValueReference* boolVr, std::size_t nBoolvr, cppfmu::FMIBoolean* boolValue,
    const cppfmu::FMIValueReference* strVr, std::size_t nStrvr, cppfmu::FMIString* strValue) const
{

    jvm_invoke(jvm_, [this, intVr, nIntvr, intValue, realVr, nRealvr, realValue, boolVr, nBoolvr, boolValue, strVr, nStrvr, strValue](JNIEnv* env) {
        if (canGetSetAll_) {
            auto intVrArray = env->NewLongArray(nIntvr);
            auto realVrArray = env->NewLongArray(nRealvr);
            auto boolVrArray = env->NewLongArray(nBoolvr);
            auto strVrArray = env->NewLongArray(nStrvr);

            auto intVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nIntvr));
            for (int i = 0; i < nIntvr; i++) {
                intVrArrayElements[i] = static_cast<jlong>(intVr[i]);
            }
            env->SetLongArrayRegion(intVrArray, 0, nIntvr, intVrArrayElements);

            auto realVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nRealvr));
            for (int i = 0; i < nRealvr; i++) {
                realVrArrayElements[i] = static_cast<jlong>(realVr[i]);
            }
            env->SetLongArrayRegion(realVrArray, 0, nRealvr, realVrArrayElements);

            auto boolVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nBoolvr));
            for (int i = 0; i < nBoolvr; i++) {
                boolVrArrayElements[i] = static_cast<jlong>(boolVr[i]);
            }
            env->SetLongArrayRegion(boolVrArray, 0, nBoolvr, boolVrArrayElements);

            auto strVrArrayElements = reinterpret_cast<jlong*>(malloc(sizeof(jlong) * nStrvr));
            for (int i = 0; i < nStrvr; i++) {
                strVrArrayElements[i] = static_cast<jlong>(strVr[i]);
            }
            env->SetLongArrayRegion(strVrArray, 0, nStrvr, strVrArrayElements);

            jobject read = env->CallObjectMethod(slaveInstance_, getAllId_, intVrArray, realVrArray, boolVrArray, strVrArray);
            auto intValueArray = reinterpret_cast<jintArray>(env->CallObjectMethod(read, bulkIntValuesId_));
            auto realValueArray = reinterpret_cast<jdoubleArray>(env->CallObjectMethod(read, bulkRealValuesId_));
            auto boolValueArray = reinterpret_cast<jbooleanArray>(env->CallObjectMethod(read, bulkBoolValuesId_));
            auto strValueArray = reinterpret_cast<jobjectArray>(env->CallObjectMethod(read, bulkStrValuesId_));

            auto intArrayElements = env->GetIntArrayElements(intValueArray, nullptr);
            for (auto i = 0; i < nIntvr; i++) {
                intValue[i] = static_cast<fmi2Integer>(intArrayElements[i]);
            }

            auto realArrayElements = env->GetDoubleArrayElements(realValueArray, nullptr);
            for (auto i = 0; i < nRealvr; i++) {
                realValue[i] = realArrayElements[i];
            }

            auto boolArrayElements = env->GetBooleanArrayElements(boolValueArray, nullptr);
            for (auto i = 0; i < nBoolvr; i++) {
                boolValue[i] = static_cast<fmi2Boolean>(boolArrayElements[i]);
            }

            clearStrBuffer(env);
            for (auto i = 0; i < nStrvr; i++) {
                auto jStr = reinterpret_cast<jstring>(env->GetObjectArrayElement(strValueArray, i));
                auto cStr = env->GetStringUTFChars(jStr, nullptr);
                strValue[i] = cStr;
                jstring_ref ref{
                    cStr = cStr,
                    jStr = jStr};
                strBuffer.push_back(ref);
            }

            free(intVrArrayElements);
            free(realVrArrayElements);
            free(boolVrArrayElements);
            free(strVrArrayElements);

            env->ReleaseIntArrayElements(intValueArray, intArrayElements, 0);
            env->ReleaseDoubleArrayElements(realValueArray, realArrayElements, 0);
            env->ReleaseBooleanArrayElements(boolValueArray, boolArrayElements, 0);
        } else {
            GetInteger(intVr, nIntvr, intValue);
            GetReal(realVr, nRealvr, realValue);
            GetBoolean(boolVr, nBoolvr, boolValue);
            GetString(strVr, nStrvr, strValue);
        }
    });
}

void SlaveInstance::onClose()
{
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        clearStrBuffer(env);
        env->CallVoidMethod(slaveInstance_, closeId_);
    });
}


SlaveInstance::~SlaveInstance()
{
    onClose();
    jvm_invoke(jvm_, [this](JNIEnv* env) {
        env->DeleteGlobalRef(slaveInstance_);

        jclass URLClassLoader = env->FindClass("java/net/URLClassLoader");
        jmethodID closeId = env->GetMethodID(URLClassLoader, "close", "()V");
        env->CallVoidMethod(classLoader_, closeId);

        env->DeleteGlobalRef(classLoader_);
    });
}

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

} // namespace fmu4j

cppfmu::UniquePtr<cppfmu::SlaveInstance> CppfmuInstantiateSlave(
    cppfmu::FMIString instanceName,
    cppfmu::FMIString,
    cppfmu::FMIString fmuResourceLocation,
    cppfmu::FMIString,
    cppfmu::FMIReal,
    cppfmu::FMIBoolean,
    cppfmu::FMIBoolean,
    cppfmu::Memory memory,
    const cppfmu::Logger& logger)
{
    std::string resources(fmuResourceLocation);

    if (resources.find("file:///") != std::string::npos) {
        resources.replace(0, 8, "");
    } else if (resources.find("file://") != std::string::npos) {
        resources.replace(0, 7, "");
    } else if (resources.find("file:/") != std::string::npos) {
        resources.replace(0, 6, "");
    }

    JNIEnv* env;
    JavaVM* jvm;
    env = get_or_create_jvm(&jvm);

    if (env == nullptr) {
        throw cppfmu::FatalError("Unable to setup the JVM!");
    }

    return cppfmu::AllocateUnique<fmu4j::SlaveInstance>(memory, env, instanceName, resources);
}
