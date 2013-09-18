////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2013 Laurent Gomila (laurent.gom@gmail.com)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Audio/SoundRecorder.hpp>
#include <SFML/Audio/AudioDevice.hpp>
#include <SFML/Audio/ALCheck.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Err.hpp>
#include <cstring>

#ifdef _MSC_VER
    #pragma warning(disable : 4355) // 'this' used in base member initializer list
#endif


namespace
{
    ALCdevice* captureDevice = NULL;
}

namespace sf
{
////////////////////////////////////////////////////////////
SoundRecorder::SoundRecorder() :
m_thread            (&SoundRecorder::record, this),
m_sampleRate        (0),
m_isCapturing       (false),
m_processingInterval(milliseconds(100))
{
    priv::ensureALInit();

    // Set the device name to the default device
    m_deviceName = getDefaultDevice();
}


////////////////////////////////////////////////////////////
SoundRecorder::~SoundRecorder()
{
    // Nothing to do
}


////////////////////////////////////////////////////////////
bool SoundRecorder::start(unsigned int sampleRate)
{
    // Check if the device can do audio capture
    if (!isAvailable())
    {
        err() << "Failed to start capture : your system cannot capture audio data (call SoundRecorder::isAvailable to check it)" << std::endl;
        return false;
    }

    // Check that another capture is not already running
    if (captureDevice)
    {
        err() << "Trying to start audio capture, but another capture is already running" << std::endl;
        return false;
    }

    // Open the capture device for capturing 16 bits mono samples
    captureDevice = alcCaptureOpenDevice(m_deviceName.c_str(), sampleRate, AL_FORMAT_MONO16, sampleRate);
    if (!captureDevice)
    {
        err() << "Failed to open the audio capture device with the name: " << m_deviceName << std::endl;
        return false;
    }

    // Clear the array of samples
    m_samples.clear();

    // Store the sample rate
    m_sampleRate = sampleRate;

    // Notify derived class
    if (onStart())
    {
        // Start the capture
        alcCaptureStart(captureDevice);

        // Start the capture in a new thread, to avoid blocking the main thread
        m_isCapturing = true;
        m_thread.launch();

        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////
void SoundRecorder::stop()
{
    // Stop the capturing thread
    m_isCapturing = false;
    m_thread.wait();

    // Notify derived class
    onStop();
}


////////////////////////////////////////////////////////////
unsigned int SoundRecorder::getSampleRate() const
{
    return m_sampleRate;
}


////////////////////////////////////////////////////////////
std::vector<std::string> SoundRecorder::getAvailableDevices()
{
    std::vector<std::string> deviceNameList;

    const ALchar *deviceList = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
    if (deviceList)
    {
        while (*deviceList)
        {
            deviceNameList.push_back(deviceList);
            deviceList += std::strlen(deviceList) + 1;
        }
    }

    return deviceNameList;
}


////////////////////////////////////////////////////////////
std::string SoundRecorder::getDefaultDevice()
{
    return alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
}


////////////////////////////////////////////////////////////
bool SoundRecorder::setDevice(const std::string& name)
{
    // Store the device name
    if (name.empty())
        m_deviceName = getDefaultDevice();
    else
        m_deviceName = name;

    if (m_isCapturing)
    {
        // Stop the capturing thread
        m_isCapturing = false;
        m_thread.wait();

        // Open the requested capture device for capturing 16 bits mono samples
        captureDevice = alcCaptureOpenDevice(name.c_str(), m_sampleRate, AL_FORMAT_MONO16, m_sampleRate);
        if (!captureDevice)
        {
            // Notify derived class
            onStop();

            err() << "Failed to open the audio capture device with the name: " << m_deviceName << std::endl;
            return false;
        }

        // Start the capture
        alcCaptureStart(captureDevice);

        // Start the capture in a new thread, to avoid blocking the main thread
        m_isCapturing = true;
        m_thread.launch();
    }

    return true;
}


////////////////////////////////////////////////////////////
const std::string& SoundRecorder::getDevice() const
{
    return m_deviceName;
}


////////////////////////////////////////////////////////////
bool SoundRecorder::isAvailable()
{
    return (priv::AudioDevice::isExtensionSupported("ALC_EXT_CAPTURE") != AL_FALSE) ||
           (priv::AudioDevice::isExtensionSupported("ALC_EXT_capture") != AL_FALSE); // "bug" in Mac OS X 10.5 and 10.6
}


////////////////////////////////////////////////////////////
void SoundRecorder::setProcessingInterval(sf::Time interval)
{
    m_processingInterval = interval;
}


////////////////////////////////////////////////////////////
bool SoundRecorder::onStart()
{
    // Nothing to do
    return true;
}


////////////////////////////////////////////////////////////
void SoundRecorder::onStop()
{
    // Nothing to do
}


////////////////////////////////////////////////////////////
void SoundRecorder::record()
{
    while (m_isCapturing)
    {
        // Process available samples
        processCapturedSamples();

        // Don't bother the CPU while waiting for more captured data
        sleep(m_processingInterval);
    }

    // Capture is finished : clean up everything
    cleanup();
}


////////////////////////////////////////////////////////////
void SoundRecorder::processCapturedSamples()
{
    // Get the number of samples available
    ALCint samplesAvailable;
    alcGetIntegerv(captureDevice, ALC_CAPTURE_SAMPLES, 1, &samplesAvailable);

    if (samplesAvailable > 0)
    {
        // Get the recorded samples
        m_samples.resize(samplesAvailable);
        alcCaptureSamples(captureDevice, &m_samples[0], samplesAvailable);

        // Forward them to the derived class
        if (!onProcessSamples(&m_samples[0], m_samples.size()))
        {
            // The user wants to stop the capture
            m_isCapturing = false;
        }
    }
}


////////////////////////////////////////////////////////////
void SoundRecorder::cleanup()
{
    // Stop the capture
    alcCaptureStop(captureDevice);

    // Get the samples left in the buffer
    processCapturedSamples();

    // Close the device
    alcCaptureCloseDevice(captureDevice);
    captureDevice = NULL;
}

} // namespace sf
