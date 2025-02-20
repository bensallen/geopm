/*
 * Copyright (c) 2015 - 2022, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <string>
#include <iostream>
#include <map>

#include "geopm/Exception.hpp"
#include "geopm/Agg.hpp"
#include "geopm/Helper.hpp"

#include "LevelZeroImp.hpp"

namespace geopm
{
    const LevelZero &levelzero()
    {
        static LevelZeroImp instance;
        return instance;
    }

    LevelZeroImp::LevelZeroImp()
    {
        setenv("ZES_ENABLE_SYSMAN", "1", 1);

        ze_result_t ze_result;
        //Initialize
        ze_result = zeInit(0);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": LevelZero Driver failed to initialize.", __LINE__);

        // Discover drivers
        uint32_t num_driver = 0;
        ze_result = zeDriverGet(&num_driver, nullptr);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                        "LevelZero::" + std::string(__func__) +
                        ": LevelZero Driver enumeration failed.", __LINE__);
        m_levelzero_driver.resize(num_driver);
        ze_result = zeDriverGet(&num_driver, m_levelzero_driver.data());
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                        "LevelZero::" + std::string(__func__) +
                        ": LevelZero Driver acquisition failed.", __LINE__);

        for (unsigned int driver = 0; driver < num_driver; driver++) {
            // Discover devices in a driver
            uint32_t num_device = 0;

            ze_result = zeDeviceGet(m_levelzero_driver.at(driver), &num_device, nullptr);
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": LevelZero Device enumeration failed.", __LINE__);
            std::vector<zes_device_handle_t> device_handle(num_device);
            ze_result = zeDeviceGet(m_levelzero_driver.at(driver),
                                    &num_device, device_handle.data());
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": LevelZero Device acquisition failed.", __LINE__);

            for (unsigned int device_idx = 0; device_idx < num_device; ++device_idx) {
                ze_device_properties_t property;
                ze_result = zeDeviceGetProperties(device_handle.at(device_idx), &property);
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                                + std::string(__func__) +
                                ": failed to get device properties.", __LINE__);

                uint32_t num_subdevice = 0;

                ze_result = zeDeviceGetSubDevices(device_handle.at(device_idx),
                                                  &num_subdevice, nullptr);
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                                "LevelZero::" + std::string(__func__) +
                                ": LevelZero Sub-Device enumeration failed.", __LINE__);

                std::vector<zes_device_handle_t> subdevice_handle(num_subdevice);
                ze_result = zeDeviceGetSubDevices(device_handle.at(device_idx),
                                                  &num_subdevice, subdevice_handle.data());
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                                "LevelZero::" + std::string(__func__) +
                                ": LevelZero Sub-Device acquisition failed.", __LINE__);
#ifdef GEOPM_DEBUG
                if (num_subdevice == 0) {
                    std::cerr << "LevelZero::" << std::string(__func__)  <<
                                 ": GEOPM Requires at least one subdevice. "
                                 "Please check ZE_AFFINITY_MASK enviroment variable "
                                 "setting.  Forcing device to act as sub-device" << std::endl;
                }
#endif
                if (property.type == ZE_DEVICE_TYPE_GPU) {
                    if ((property.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED) == 0) {
                        ++m_num_board_gpu;
                        m_num_board_gpu_subdevice += num_subdevice;
                        if (num_subdevice == 0) {
                            // If there are no subdevices we are going to treat the
                            // device as a subdevice.
                            m_num_board_gpu_subdevice += 1;
                        }

                        //NOTE: We're only supporting Board GPUs to start with
                        m_devices.push_back({
                            device_handle.at(device_idx),
                            property,
                            num_subdevice, //if there are no subdevices leave this as 0
                            subdevice_handle,
                            {}, //subdevice
                            {}, //power domain
                        });
                    }
#ifdef GEOPM_DEBUG
                    else {
                        std::cerr << "Warning: <geopm> LevelZero: Integrated "
                                     "GPU access is not currently supported by GEOPM.\n";
                    }
#endif
                }
#ifdef GEOPM_DEBUG
                else if (property.type == ZE_DEVICE_TYPE_CPU) {
                    // All CPU functionality is handled by GEOPM & MSR Safe currently
                    std::cerr << "Warning: <geopm> LevelZero: CPU access "
                                 "via LevelZero is not currently supported by GEOPM.\n";
                }
                else if (property.type == ZE_DEVICE_TYPE_FPGA) {
                    // FPGA functionality is not currently supported by GEOPM, but should not cause
                    // an error if the devices are present
                    std::cerr << "Warning: <geopm> LevelZero: Field Programmable "
                                 "Gate Arrays are not currently supported by GEOPM.\n";
                }
                else if (property.type == ZE_DEVICE_TYPE_MCA) {
                    // MCA functionality is not currently supported by GEOPM, but should not cause
                    // an error if the devices are present
                    std::cerr << "Warning: <geopm> LevelZero: Memory Copy Accelerators "
                                 "are not currently supported by GEOPM.\n";
                }
#endif
            }

            if (m_num_board_gpu_subdevice % m_num_board_gpu != 0) {
                throw Exception("LevelZero::" + std::string(__func__) +
                                ": GEOPM Requires the number of subdevices to be" +
                                " evenly divisible by the number of devices. " +
                                " Please check ZE_AFFINITY_MASK enviroment variable settings",
                                GEOPM_ERROR_INVALID, __FILE__, __LINE__);
            }
        }

        // TODO: When additional device types such as FPGA, MCA, and Integrated GPU are supported by GEOPM
        // This should be changed to a more general loop iterating over type and caching appropriately
        for (unsigned int board_gpu_idx = 0; board_gpu_idx < m_num_board_gpu; board_gpu_idx++) {
            domain_cache(board_gpu_idx);
       }
    }

    void LevelZeroImp::domain_cache(unsigned int device_idx) {
        ze_result_t ze_result;
        uint32_t num_domain = 0;

        //Cache frequency domains
        ze_result = zesDeviceEnumFrequencyDomains(m_devices.at(
                                                  device_idx).device_handle,
                                                  &num_domain, nullptr);
        if (ze_result == ZE_RESULT_ERROR_UNSUPPORTED_FEATURE) {
#ifdef GEOPM_DEBUG
            std::cerr << "Warning: <geopm> LevelZero: Frequency domain detection is "
                         "not supported.\n";
#endif
        }
        else {
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": Sysman failed to get number of domains.", __LINE__);
            //make temp var
            std::vector<zes_freq_handle_t> freq_domain(num_domain);

            ze_result = zesDeviceEnumFrequencyDomains(m_devices.at(
                            device_idx).device_handle, &num_domain, freq_domain.data());
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": Sysman failed to get domain handles.", __LINE__);

            m_devices.at(device_idx).subdevice.freq_domain.resize(
                            geopm::LevelZero::M_DOMAIN_SIZE);

            for (auto handle : freq_domain) {
                zes_freq_properties_t property;
                ze_result = zesFrequencyGetProperties(handle, &property);
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                                "LevelZero::" + std::string(__func__) +
                                ": Sysman failed to get domain properties.", __LINE__);

                if (property.onSubdevice == 0 && m_devices.at(device_idx).m_num_subdevice != 0) {
#ifdef GEOPM_DEBUG
                    std::cerr << "Warning: <geopm> LevelZero: A device level "
                                 "frequency domain was found but is not currently supported.\n";
#endif
                }
                else {
                    if (property.type == ZES_FREQ_DOMAIN_GPU) {
                        m_devices.at(device_idx).subdevice.freq_domain.at(
                                  geopm::LevelZero::M_DOMAIN_COMPUTE).push_back(handle);
                    }
                    else if (property.type == ZES_FREQ_DOMAIN_MEMORY) {
                        m_devices.at(device_idx).subdevice.freq_domain.at(
                                  geopm::LevelZero::M_DOMAIN_MEMORY).push_back(handle);
                    }
                }
            }
        }

        //Cache power domains
        num_domain = 0;
        ze_result = zesDeviceEnumPowerDomains(m_devices.at(
                    device_idx).device_handle, &num_domain, nullptr);
        if (ze_result == ZE_RESULT_ERROR_UNSUPPORTED_FEATURE) {
#ifdef GEOPM_DEBUG
            std::cerr << "Warning: <geopm> LevelZero: Power domain detection is "
                         "not supported.\n";
#endif
        }
        else {
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": Sysman failed to get number of domains", __LINE__);

            std::vector<zes_pwr_handle_t> power_domain(num_domain);

            ze_result = zesDeviceEnumPowerDomains(m_devices.at(
                            device_idx).device_handle, &num_domain, power_domain.data());
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                            + std::string(__func__) +
                            ": Sysman failed to get domain handle(s).", __LINE__);

            int num_device_power_domain = 0;
            for (auto handle : power_domain) {
                zes_power_properties_t property;
                ze_result = zesPowerGetProperties(handle, &property);
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                                "LevelZero::" + std::string(__func__) +
                                ": Sysman failed to get domain power properties",
                                __LINE__);

                //Finding non-subdevice domain.
                if (property.onSubdevice == 0) {
                    m_devices.at(device_idx).power_domain = handle;
                    ++num_device_power_domain;
                }
#ifdef GEOPM_DEBUG
                else {
                    //For initial GEOPM support we're only providing device level power
                    std::cerr << "Warning: <geopm> LevelZero: A sub-device "
                                 "level power domain was found but is not currently supported.\n";
                }
#endif

                if (num_device_power_domain != 1) {
                    throw Exception("LevelZero::" + std::string(__func__) +
                                    ": GEOPM requires a single device level power domain, "
                                    "but found " + std::to_string(num_device_power_domain),
                                    GEOPM_ERROR_INVALID, __FILE__, __LINE__);
                }
            }
        }

        //Cache engine domains
        num_domain = 0;
        ze_result = zesDeviceEnumEngineGroups(m_devices.at(device_idx).device_handle,
                                              &num_domain, nullptr);

        if (ze_result == ZE_RESULT_ERROR_UNSUPPORTED_FEATURE) {
#ifdef GEOPM_DEBUG
            std::cerr << "Warning: <geopm> LevelZero: Engine domain detection is "
                         "not supported.\n";
#endif
        }
        else {
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::" +
                            std::string(__func__) +
                            ": Sysman failed to get number of domains", __LINE__);

            std::vector<zes_engine_handle_t> engine_domain(num_domain);

            ze_result = zesDeviceEnumEngineGroups(m_devices.at(device_idx).device_handle,
                                                  &num_domain, engine_domain.data());
            check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                            "LevelZero::" + std::string(__func__) +
                            ": Sysman failed to get number of domains", __LINE__);

            m_devices.at(device_idx).subdevice.
                      engine_domain.resize(geopm::LevelZero::M_DOMAIN_SIZE);
            m_devices.at(device_idx).subdevice.
                      cached_timestamp.resize(geopm::LevelZero::M_DOMAIN_SIZE);
            for (auto handle : engine_domain) {
                zes_engine_properties_t property;
                ze_result = zesEngineGetProperties(handle, &property);
                check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                                + std::string(__func__) +
                                ": Sysman failed to get domain engine properties",
                                __LINE__);

                if (property.onSubdevice == 0 && m_devices.at(device_idx).m_num_subdevice != 0) {
#ifdef GEOPM_DEBUG
                    std::cerr << "Warning: <geopm> LevelZero: A device level "
                                 "engine domain was found but is not currently supported.\n";
#endif
                }
                else {
                    if (property.type == ZES_ENGINE_GROUP_ALL) {
                        m_devices.at(device_idx).subdevice.engine_domain.at(
                                 geopm::LevelZero::M_DOMAIN_ALL).push_back(handle);
                        m_devices.at(device_idx).subdevice.cached_timestamp.at(
                                 geopm::LevelZero::M_DOMAIN_ALL).push_back(0);
                    }

                    //TODO: Some devices may not support ZES_ENGINE_GROUP_COMPUTE/COPY_ALL.
                    //      We can do a check for COMPUTE_ALL and then fallback to change to
                    //      ZES_ENGINE_GROUP_COMPUTE/COPY_SINGLE, but we have to
                    //      aggregate the signals in that case
                    else if (property.type == ZES_ENGINE_GROUP_COMPUTE_ALL) {
                        m_devices.at(device_idx).subdevice.engine_domain.at(
                                     geopm::LevelZero::M_DOMAIN_COMPUTE).push_back(handle);
                        m_devices.at(device_idx).subdevice.cached_timestamp.at(
                                 geopm::LevelZero::M_DOMAIN_COMPUTE).push_back(0);
                    }
                    else if (property.type == ZES_ENGINE_GROUP_COPY_ALL) {
                        m_devices.at(device_idx).subdevice.engine_domain.at(
                                     geopm::LevelZero::M_DOMAIN_MEMORY).push_back(handle);
                        m_devices.at(device_idx).subdevice.cached_timestamp.at(
                                 geopm::LevelZero::M_DOMAIN_MEMORY).push_back(0);
                    }
                }
            }

#ifdef GEOPM_DEBUG
            if (num_domain != 0 &&
                m_devices.at(device_idx).subdevice.engine_domain.at(
                             geopm::LevelZero::M_DOMAIN_COMPUTE).size() == 0) {
                std::cerr << "Warning: <geopm> LevelZero: Engine domain detection "
                             "did not find ZES_ENGINE_GROUP_COMPUTE_ALL.\n";
            }
            if (num_domain != 0 &&
                m_devices.at(device_idx).subdevice.engine_domain.at(
                             geopm::LevelZero::M_DOMAIN_MEMORY).size() == 0) {
                std::cerr << "Warning: <geopm> LevelZero: Engine domain detection "
                             "did not find ZES_ENGINE_GROUP_COPY_ALL.\n";
            }
#endif
        }
    }

    int LevelZeroImp::num_accelerator() const
    {
        //  TODO: this should be expanded to return all supported accel types.
        //  Right now that is only board_gpus
        return num_accelerator(GEOPM_DOMAIN_BOARD_ACCELERATOR);
    }

    int LevelZeroImp::num_accelerator(int domain_type) const
    {
        //  TODO: this should be expanded to return all supported accel types.
        //  Right now that is only board_gpus
        int result = -1;
        switch(domain_type) {
            case GEOPM_DOMAIN_BOARD_ACCELERATOR:
                result = m_num_board_gpu;
                break;
            case GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP:
                result = m_num_board_gpu_subdevice;
                break;
            default :
                throw Exception("LevelZero::" + std::string(__func__) +
                                ": domain type " + std::to_string(domain_type) +
                                " is not supported.", GEOPM_ERROR_INVALID,
                                 __FILE__, __LINE__);
                break;
        }
        return result;
    }

    int LevelZeroImp::frequency_domain_count(unsigned int l0_device_idx, int l0_domain) const
    {
        return m_devices.at(l0_device_idx).subdevice.freq_domain.at(l0_domain).size();
    }

    int LevelZeroImp::engine_domain_count(unsigned int l0_device_idx, int l0_domain) const
    {
        return m_devices.at(l0_device_idx).subdevice.engine_domain.at(l0_domain).size();
    }

    double LevelZeroImp::frequency_status(unsigned int l0_device_idx,
                                          int l0_domain, int l0_domain_idx) const
    {
        return frequency_status_helper(l0_device_idx, l0_domain, l0_domain_idx).actual;
    }

    LevelZeroImp::m_frequency_s LevelZeroImp::frequency_status_helper(unsigned int l0_device_idx,
                                                                      int l0_domain, int l0_domain_idx) const
    {
        ze_result_t ze_result;
        m_frequency_s result;

        zes_freq_handle_t handle = m_devices.at(l0_device_idx).
                                             subdevice.freq_domain.at(
                                             l0_domain).at(l0_domain_idx);
        zes_freq_state_t state;
        ze_result = zesFrequencyGetState(handle, &state);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                        "LevelZero::" + std::string(__func__) +
                        ": Sysman failed to get frequency state", __LINE__);

        result.voltage = state.currentVoltage;
        result.request = state.request;
        result.tdp = state.tdp;
        result.efficient = state.efficient;
        result.actual = state.actual;
        result.throttle_reasons = state.throttleReasons;

        return result;
    }

    double LevelZeroImp::frequency_min(unsigned int l0_device_idx,
                                       int l0_domain, int l0_domain_idx) const
    {
        return frequency_min_max(l0_device_idx, l0_domain, l0_domain_idx).first;
    }

    double LevelZeroImp::frequency_max(unsigned int l0_device_idx,
                                       int l0_domain, int l0_domain_idx) const
    {
        return frequency_min_max(l0_device_idx, l0_domain, l0_domain_idx).second;
    }

    std::pair<double, double> LevelZeroImp::frequency_min_max(unsigned int l0_device_idx,
                                                              int l0_domain,
                                                              int l0_domain_idx) const
    {
        ze_result_t ze_result;
        zes_freq_handle_t handle = m_devices.at(l0_device_idx).subdevice.freq_domain.at(
                                                               l0_domain).at(l0_domain_idx);
        zes_freq_properties_t property;
        ze_result = zesFrequencyGetProperties(handle, &property);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": Sysman failed to get domain properties.", __LINE__);

        return {property.min, property.max};
    }

    std::pair<double, double> LevelZeroImp::frequency_range(unsigned int l0_device_idx,
                                                            int l0_domain,
                                                            int l0_domain_idx) const
    {
        ze_result_t ze_result;
        zes_freq_handle_t handle = m_devices.at(l0_device_idx).subdevice.freq_domain.at(
                                                               l0_domain).at(l0_domain_idx);
        zes_freq_range_t range;
        ze_result = zesFrequencyGetRange(handle, &range);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": Sysman failed to get frequency range.", __LINE__);
        return {range.min, range.max};
    }

    uint64_t LevelZeroImp::active_time_timestamp(unsigned int l0_device_idx,
                                                 int l0_domain, int l0_domain_idx) const
    {
        return m_devices.at(l0_device_idx).subdevice.cached_timestamp.at(
                        l0_domain).at(l0_domain_idx);
    }

    uint64_t LevelZeroImp::active_time(unsigned int l0_device_idx,
                                       int l0_domain, int l0_domain_idx) const
    {
        return active_time_pair(l0_device_idx, l0_domain, l0_domain_idx).first;
    }

    std::pair<uint64_t,uint64_t> LevelZeroImp::active_time_pair(unsigned int l0_device_idx,
                                                                int l0_domain,
                                                                int l0_domain_idx) const
    {
        ze_result_t ze_result;
        uint64_t result_active = 0;
        uint64_t result_timestamp = 0;

        zes_engine_stats_t stats;

        zes_engine_handle_t handle = m_devices.at(l0_device_idx).subdevice.
                                     engine_domain.at(l0_domain).at(l0_domain_idx);
        ze_result = zesEngineGetActivity(handle, &stats);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": Sysman failed to get engine group activity.", __LINE__);
        result_active = stats.activeTime;
        result_timestamp = stats.timestamp;
        m_devices.at(l0_device_idx).subdevice.cached_timestamp.at(
                        l0_domain).at(l0_domain_idx) = result_timestamp;

        return {result_active, result_timestamp};
    }

    uint64_t LevelZeroImp::energy_timestamp(unsigned int l0_device_idx) const
    {
        return m_devices.at(l0_device_idx).cached_energy_timestamp;
    }

    uint64_t LevelZeroImp::energy(unsigned int l0_device_idx) const
    {
        return energy_pair(l0_device_idx).first;
    }

    std::pair<uint64_t,uint64_t> LevelZeroImp::energy_pair(unsigned int l0_device_idx) const
    {
        ze_result_t ze_result;
        uint64_t result_energy = 0;
        uint64_t result_timestamp = 0;

        zes_pwr_handle_t handle = m_devices.at(l0_device_idx).power_domain;
        zes_power_energy_counter_t energy_counter;
        ze_result = zesPowerGetEnergyCounter(handle, &energy_counter);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": Sysman failed to get energy_counter values", __LINE__);
        result_energy += energy_counter.energy;
        result_timestamp += energy_counter.timestamp;
        m_devices.at(l0_device_idx).cached_energy_timestamp = result_timestamp;

        return {result_energy, result_timestamp};
    }

    int32_t LevelZeroImp::power_limit_tdp(unsigned int l0_device_idx) const
    {
        return power_limit_default(l0_device_idx).tdp;
    }

    int32_t LevelZeroImp::power_limit_min(unsigned int l0_device_idx) const
    {
        return power_limit_default(l0_device_idx).min;
    }

    int32_t LevelZeroImp::power_limit_max(unsigned int l0_device_idx) const
    {
        return power_limit_default(l0_device_idx).max;
    }

    LevelZeroImp::m_power_limit_s LevelZeroImp::power_limit_default(unsigned int l0_device_idx) const
    {
        ze_result_t ze_result;

        zes_power_properties_t property;
        m_power_limit_s result_power;

        zes_pwr_handle_t handle = m_devices.at(l0_device_idx).power_domain;

        ze_result = zesPowerGetProperties(handle, &property);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZeroDevicePool::"
                        + std::string(__func__) +
                        ": Sysman failed to get domain power properties", __LINE__);
        result_power.tdp = property.defaultLimit;
        result_power.min = property.minLimit;
        result_power.max = property.maxLimit;

        return result_power;
    }

    //TODO: frequency_control_min and frequency_control_max capability will be required in some form for save/restore
    void LevelZeroImp::frequency_control(unsigned int l0_device_idx, int l0_domain,
                                         int l0_domain_idx, double range_min,
                                         double range_max) const
    {
        ze_result_t ze_result;
        zes_freq_properties_t property;
        zes_freq_range_t range;
        range.min = range_min;
        range.max = range_max;

        zes_freq_handle_t handle = m_devices.at(l0_device_idx).subdevice.
                                   freq_domain.at(l0_domain).at(l0_domain_idx);

        ze_result = zesFrequencyGetProperties(handle, &property);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME, "LevelZero::"
                        + std::string(__func__) +
                        ": Sysman failed to get domain properties.", __LINE__);
        if (property.canControl == 0) {
            throw Exception("LevelZero::" + std::string(__func__) +
                            ": Attempted to set frequency " +
                            "for non controllable domain",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        ze_result = zesFrequencySetRange(handle, &range);
        check_ze_result(ze_result, GEOPM_ERROR_RUNTIME,
                        "LevelZero::" + std::string(__func__) +
                        ": Sysman failed to set frequency.", __LINE__);
    }

    void LevelZeroImp::check_ze_result(ze_result_t ze_result, int error,
                                       std::string message, int line) const
    {
        std::map<ze_result_t, std::string> error_mapping =
            {{ZE_RESULT_SUCCESS,
              "ZE_RESULT_SUCCESS"},
              {ZE_RESULT_NOT_READY,
               "ZE_RESULT_NOT_READY"},
              {ZE_RESULT_ERROR_UNINITIALIZED,
               "ZE_RESULT_ERROR_UNINITIALIZED"},
              {ZE_RESULT_ERROR_DEVICE_LOST,
               "ZE_RESULT_ERROR_DEVICE_LOST"},
              {ZE_RESULT_ERROR_INVALID_ARGUMENT,
               "ZE_RESULT_ERROR_INVALID_ARGUMENT"},
              {ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY,
               "ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY"},
              {ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY,
               "ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY"},
              {ZE_RESULT_ERROR_MODULE_BUILD_FAILURE,
               "ZE_RESULT_ERROR_MODULE_BUILD_FAILURE"},
              {ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS,
               "ZE_RESULT_ERROR_INSUFFICIENT_PERMISSIONS"},
              {ZE_RESULT_ERROR_NOT_AVAILABLE,
               "ZE_RESULT_ERROR_NOT_AVAILABLE"},
              {ZE_RESULT_ERROR_UNSUPPORTED_VERSION,
               "ZE_RESULT_ERROR_UNSUPPORTED_VERSION"},
              {ZE_RESULT_ERROR_UNSUPPORTED_FEATURE,
               "ZE_RESULT_ERROR_UNSUPPORTED_FEATURE"},
              {ZE_RESULT_ERROR_INVALID_NULL_HANDLE,
               "ZE_RESULT_ERROR_INVALID_NULL_HANDLE"},
              {ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE,
               "ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE"},
              {ZE_RESULT_ERROR_INVALID_NULL_POINTER,
               "ZE_RESULT_ERROR_INVALID_NULL_POINTER"},
              {ZE_RESULT_ERROR_INVALID_SIZE,
               "ZE_RESULT_ERROR_INVALID_SIZE"},
              {ZE_RESULT_ERROR_UNSUPPORTED_SIZE,
               "ZE_RESULT_ERROR_UNSUPPORTED_SIZE"},
              {ZE_RESULT_ERROR_UNSUPPORTED_ALIGNMENT,
               "ZE_RESULT_ERROR_UNSUPPORTED_ALIGNMENT"},
              {ZE_RESULT_ERROR_INVALID_SYNCHRONIZATION_OBJECT,
               "ZE_RESULT_ERROR_INVALID_SYNCHRONIZATION_OBJECT"},
              {ZE_RESULT_ERROR_INVALID_ENUMERATION,
               "ZE_RESULT_ERROR_INVALID_ENUMERATION"},
              {ZE_RESULT_ERROR_UNSUPPORTED_ENUMERATION,
               "ZE_RESULT_ERROR_UNSUPPORTED_ENUMERATION"},
              {ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT,
               "ZE_RESULT_ERROR_UNSUPPORTED_IMAGE_FORMAT"},
              {ZE_RESULT_ERROR_INVALID_NATIVE_BINARY,
               "ZE_RESULT_ERROR_INVALID_NATIVE_BINARY"},
              {ZE_RESULT_ERROR_INVALID_GLOBAL_NAME,
               "ZE_RESULT_ERROR_INVALID_GLOBAL_NAME"},
              {ZE_RESULT_ERROR_INVALID_KERNEL_NAME,
               "ZE_RESULT_ERROR_INVALID_KERNEL_NAME"},
              {ZE_RESULT_ERROR_INVALID_FUNCTION_NAME,
               "ZE_RESULT_ERROR_INVALID_FUNCTION_NAME"},
              {ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION,
               "ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION"},
              {ZE_RESULT_ERROR_INVALID_GLOBAL_WIDTH_DIMENSION,
               "ZE_RESULT_ERROR_INVALID_GLOBAL_WIDTH_DIMENSION"},
              {ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_INDEX,
               "ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_INDEX"},
              {ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_SIZE,
               "ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_SIZE"},
              {ZE_RESULT_ERROR_INVALID_KERNEL_ATTRIBUTE_VALUE,
               "ZE_RESULT_ERROR_INVALID_KERNEL_ATTRIBUTE_VALUE"},
              {ZE_RESULT_ERROR_INVALID_COMMAND_LIST_TYPE,
               "ZE_RESULT_ERROR_INVALID_COMMAND_LIST_TYPE"},
              {ZE_RESULT_ERROR_OVERLAPPING_REGIONS,
               "ZE_RESULT_ERROR_OVERLAPPING_REGIONS"},
              {ZE_RESULT_ERROR_UNKNOWN,
               "ZE_RESULT_ERROR_UNKNOWN"},
            };
        std::string error_string = error_mapping.at(ze_result);
        if (ze_result != ZE_RESULT_SUCCESS) {
            throw Exception(message + " Level Zero Error: " + error_string,
                            error, __FILE__, line);
        }
    }
}
