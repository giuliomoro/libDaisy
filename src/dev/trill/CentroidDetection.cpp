#include "CentroidDetection.h"

// a small helper class, whose main purpose is to wrap the #include
// and make all the variables related to it private and multi-instance safe
class CentroidDetection::CalculateCentroids
{
  public:
    typedef CentroidDetection::WORD WORD;
    typedef uint8_t                 BYTE;
    typedef uint8_t                 BOOL;
    WORD*                           CSD_waSnsDiff;
    WORD                            wMinimumCentroidSize = 0;
    BYTE                            SLIDER_BITS          = 16;
    WORD                            wAdjacentCentroidNoiseThreshold
        = 400; // Trough between peaks needed to identify two centroids
               // calculateCentroids is defined here:
#include "calculateCentroids.h"
    void processCentroids(WORD* wVCentroid,
                          WORD* wVCentroidSize,
                          BYTE  MAX_NUM_CENTROIDS,
                          BYTE  FIRST_SENSOR_V,
                          BYTE  LAST_SENSOR_V,
                          BYTE  numSensors)
    {
        long temp;
        BYTE lastActiveSensor;
        BYTE counter;
        WORD posEndOfLoop = (LAST_SENSOR_V - FIRST_SENSOR_V) << SLIDER_BITS;
        temp              = calculateCentroids(wVCentroid,
                                  wVCentroidSize,
                                  MAX_NUM_CENTROIDS,
                                  FIRST_SENSOR_V,
                                  LAST_SENSOR_V,
                                  numSensors); // Vertical centroids
        lastActiveSensor  = temp >> 8;

        temp = lastActiveSensor
               - (LAST_SENSOR_V
                  - FIRST_SENSOR_V); // retrieve the (wrapped) index
                                     //check for activity in the wraparound area
        // IF the last centroid ended after wrapping around ...
        // AND the first centroid was located before the end of the last ...
        if(lastActiveSensor != 255 // 255 means no active sensor
           && lastActiveSensor >= LAST_SENSOR_V - FIRST_SENSOR_V
           && ((unsigned)temp) << SLIDER_BITS >= wVCentroid[0])
        {
            // THEN the last touch is used to replace the first one
            for(counter = MAX_NUM_CENTROIDS - 1; counter >= 1; counter--)
            {
                if(0xFFFF == wVCentroid[counter])
                    continue;
                // replace the first centroid
                wVCentroidSize[0] = wVCentroidSize[counter];
                wVCentroid[0]     = wVCentroid[counter];
                // wrap around the position if needed
                if(wVCentroid[0] >= posEndOfLoop)
                    wVCentroid[0] -= posEndOfLoop;
                // discard the last centroid
                wVCentroid[counter]     = 0xFFFF;
                wVCentroidSize[counter] = 0x0;
                break;
            }
        }
    }
};

CentroidDetection::CentroidDetection(unsigned int numReadings,
                                     unsigned int maxNumCentroids,
                                     float        sizeScale)
{
    setup(numReadings, maxNumCentroids, sizeScale);
}

CentroidDetection::CentroidDetection(const std::vector<unsigned int>& order,
                                     unsigned int maxNumCentroids,
                                     float        sizeScale)
{
    setup(order, maxNumCentroids, sizeScale);
}

int CentroidDetection::setup(unsigned int numReadings,
                             unsigned int maxNumCentroids,
                             float        sizeScale)
{
    std::vector<unsigned int> order;
    for(unsigned int n = 0; n < numReadings; ++n)
        order.push_back(n);
    return setup(order, maxNumCentroids, sizeScale);
}

int CentroidDetection::setup(const std::vector<unsigned int>& order,
                             unsigned int                     maxNumCentroids,
                             float                            sizeScale)
{
    this->order = order;
    setWrapAround(0);
    this->maxNumCentroids = maxNumCentroids;
    centroidBuffer.resize(maxNumCentroids);
    sizeBuffer.resize(maxNumCentroids);
    centroids.resize(maxNumCentroids);
    sizes.resize(maxNumCentroids);
    data.resize(order.size());
    setSizeScale(sizeScale);
    setNoiseThreshold(0);
    cc = std::shared_ptr<CalculateCentroids>(new CalculateCentroids());
    setMultiplierBits(cc->SLIDER_BITS);
    num_touches = 0;
    return 0;
}

void CentroidDetection::setWrapAround(unsigned int n)
{
    num_sensors = order.size() + n;
}

void CentroidDetection::setMultiplierBits(unsigned int n)
{
    cc->SLIDER_BITS = n;
    locationScale   = 1.f / ((order.size() - 1) * (1 << cc->SLIDER_BITS));
}

void CentroidDetection::process(const DATA_T* rawData)
{
    for(unsigned int n = 0; n < order.size(); ++n)
    {
        float val = rawData[order[n]] * (1 << 12);
        val -= noiseThreshold;
        if(val < 0)
            val = 0;
        data[n] = val;
    }
    cc->CSD_waSnsDiff = data.data();
    cc->processCentroids(centroidBuffer.data(),
                         sizeBuffer.data(),
                         maxNumCentroids,
                         0,
                         order.size(),
                         num_sensors);

    // Look for 1st instance of 0xFFFF (no touch) in the buffer
    unsigned int i;
    for(i = 0; i < centroidBuffer.size(); ++i)
    {
        if(0xffff == centroidBuffer[i])
            break; // at the first non-touch, break
        centroids[i] = centroidBuffer[i] * locationScale;
        sizes[i]     = sizeBuffer[i] * sizeScale;
    }
    num_touches = i;
}

void CentroidDetection::setSizeScale(float sizeScale)
{
    this->sizeScale = 1.f / sizeScale;
}

void CentroidDetection::setMinimumTouchSize(DATA_T minSize)
{
    cc->wMinimumCentroidSize = minSize;
}

void CentroidDetection::setNoiseThreshold(DATA_T threshold)
{
    noiseThreshold = threshold;
}

unsigned int CentroidDetection::getNumTouches() const
{
    return num_touches;
}

CentroidDetection::DATA_T
CentroidDetection::touchLocation(unsigned int touch_num) const
{
    if(touch_num < maxNumCentroids)
        return centroids[touch_num];
    else
        return 0;
}

CentroidDetection::DATA_T
CentroidDetection::touchSize(unsigned int touch_num) const
{
    if(touch_num < num_touches)
        return sizes[touch_num];
    else
        return 0;
}

// code below from Trill.cpp

#define compoundTouch(LOCATION, SIZE, TOUCHES)       \
    {                                                \
        float        avg        = 0;                 \
        float        totalSize  = 0;                 \
        unsigned int numTouches = TOUCHES;           \
        for(unsigned int i = 0; i < numTouches; i++) \
        {                                            \
            avg += LOCATION(i) * SIZE(i);            \
            totalSize += SIZE(i);                    \
        }                                            \
        if(numTouches)                               \
            avg = avg / totalSize;                   \
        return avg;                                  \
    }

CentroidDetection::DATA_T CentroidDetection::compoundTouchLocation() const
{
    compoundTouch(touchLocation, touchSize, getNumTouches());
}

CentroidDetection::DATA_T CentroidDetection::compoundTouchSize() const
{
    float size = 0;
    for(unsigned int i = 0; i < getNumTouches(); i++)
        size += touchSize(i);
    return size;
}

void CentroidDetectionScaled::setUsableRange(DATA_T min, DATA_T max)
{
    this->min = min;
    this->max = max;
}

static inline float
map(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static inline float constrain(float x, float min_val, float max_val)
{
    if(x < min_val)
        return min_val;
    if(x > max_val)
        return max_val;
    return x;
}

static inline float mapAndConstrain(float x,
                                    float in_min,
                                    float in_max,
                                    float out_min,
                                    float out_max)
{
    float value = map(x, in_min, in_max, out_min, out_max);
    value       = constrain(value, out_min, out_max);
    return value;
}

void CentroidDetectionScaled::process(const DATA_T* rawData)
{
    CentroidDetection::process(rawData);
    size_t numTouches = getNumTouches();
    for(size_t n = 0; n < numTouches; ++n)
    {
        centroids[n] = mapAndConstrain(centroids[n], min, max, 0, 1);
        sizes[n]     = std::min(sizes[n], 1.f);
    }
}
