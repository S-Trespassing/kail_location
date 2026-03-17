#ifndef GAIT_SIMULATION_H
#define GAIT_SIMULATION_H

#include <stdint.h>
#include <math.h>
#include <random>
#include <mutex>

class GaitSimulation {
public:
    static GaitSimulation& getInstance() {
        static GaitSimulation instance;
        return instance;
    }

    void setStepFrequency(float stepsPerMinute) {
        std::lock_guard<std::mutex> lock(mtx);
        this->stepsPerMinute = stepsPerMinute;
        this->stepFrequency = stepsPerMinute / 60.0f;
        this->angularVelocity = 2.0f * M_PI * stepFrequency;
    }

    void setEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mtx);
        this->enabled = enabled;
    }

    // 生成加速度数据
    void generateAccelerometer(int64_t timestampNs, float* data) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!enabled || stepFrequency <= 0) {
            data[0] = 0.0f;
            data[1] = 0.0f;
            data[2] = 9.8f;
            return;
        }

        double t = (double)timestampNs / 1e9;
        
        // 增加随机扰动
        float jitter = 1.0f + dist(gen) * 0.05f; // ±5%
        float phase = angularVelocity * t * jitter;

        // 步态模拟公式
        // x = sin(t * step_frequency) * 0.6
        // y = cos(t * step_frequency) * 0.4
        // z = 9.8 + sin(t * step_frequency * 2) * 0.2
        data[0] = sin(phase) * 0.6f;
        data[1] = cos(phase) * 0.4f;
        data[2] = 9.8f + sin(phase * 2.0f) * 0.2f;
    }

    // 模拟步数累加
    uint64_t updateStepCounter(int64_t timestampNs) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!enabled || stepFrequency <= 0) return lastStepCount;

        if (lastTimestampNs == 0) {
            lastTimestampNs = timestampNs;
            return lastStepCount;
        }

        double deltaTime = (double)(timestampNs - lastTimestampNs) / 1e9;
        if (deltaTime > 0) {
            stepAccumulator += stepFrequency * deltaTime;
            if (stepAccumulator >= 1.0) {
                uint64_t newSteps = (uint64_t)stepAccumulator;
                lastStepCount += newSteps;
                stepAccumulator -= newSteps;
                hasNewStep = true;
            }
        }
        lastTimestampNs = timestampNs;
        return lastStepCount;
    }

    bool consumeStepDetected() {
        std::lock_guard<std::mutex> lock(mtx);
        bool detected = hasNewStep;
        hasNewStep = false;
        return detected;
    }

private:
    GaitSimulation() : 
        enabled(false), 
        stepsPerMinute(0), 
        stepFrequency(0), 
        angularVelocity(0),
        lastStepCount(0),
        stepAccumulator(0),
        lastTimestampNs(0),
        hasNewStep(false),
        gen(rd()),
        dist(-1.0f, 1.0f) {}

    bool enabled;
    float stepsPerMinute;
    float stepFrequency;
    float angularVelocity;
    
    uint64_t lastStepCount;
    double stepAccumulator;
    int64_t lastTimestampNs;
    bool hasNewStep;

    std::mutex mtx;
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dist;
};

#endif // GAIT_SIMULATION_H
