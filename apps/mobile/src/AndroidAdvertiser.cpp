#include "AndroidAdvertiser.h"

#include <ModulesManager.h>

AndroidAdvertiser::AndroidAdvertiser(QObject* parent)
    : QObject(parent)
{
}

AndroidAdvertiser::~AndroidAdvertiser()
{
}

void AndroidAdvertiser::start()
{
    if (m_running)
        return;

    ModulesManager::SetMainServiceBackendEnabled(true);
    m_running = true;
    emit runningChanged();
}

void AndroidAdvertiser::stop()
{
    if (!m_running)
        return;

    ModulesManager::SetMainServiceBackendEnabled(false);
    m_running = false;
    emit runningChanged();
}

void AndroidAdvertiser::acquireMulticastLock()
{
}

void AndroidAdvertiser::releaseMulticastLock()
{

}
