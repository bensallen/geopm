/*
 * Copyright (c) 2015, 2016, 2017, 2018, 2019, 2020, Intel Corporation
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

#include "LevelZeroIOGroup.hpp"

#include <cmath>

#include <iostream>
#include <string>
#include <numeric>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "geopm/IOGroup.hpp"
#include "Signal.hpp"
#include "DerivativeSignal.hpp"
#include "geopm/PlatformTopo.hpp"
#include "LevelZeroDevicePool.hpp"
#include "LevelZero.hpp"
#include "geopm/Exception.hpp"
#include "geopm/Agg.hpp"
#include "geopm/Helper.hpp"
#include "geopm_debug.hpp"
#include "SaveControl.hpp"

namespace geopm
{

    const std::string LevelZeroIOGroup::M_PLUGIN_NAME = "LEVELZERO";
    const std::string LevelZeroIOGroup::M_NAME_PREFIX = M_PLUGIN_NAME + "::";

    LevelZeroIOGroup::LevelZeroIOGroup()
        : LevelZeroIOGroup(platform_topo(), levelzero_device_pool(), nullptr)
    {
    }

    // Set up mapping between signal and control names and corresponding indices
    LevelZeroIOGroup::LevelZeroIOGroup(const PlatformTopo &platform_topo,
                                       const LevelZeroDevicePool &device_pool,
                                       std::shared_ptr<SaveControl> save_control)
        : m_platform_topo(platform_topo)
        , m_levelzero_device_pool(device_pool)
        , m_is_batch_read(false)
        , m_signal_available({{M_NAME_PREFIX + "GPUCHIP_FREQUENCY_STATUS", {
                                  "Compute/GPU chip domain current frequency in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      // Note: Only the domain index is changing
                                      //       here when signals are generated in
                                      //       the init function. Everything else
                                      //       is provided as part of this initial
                                      //       declaration and does not change per
                                      //       signal.
                                      return this->m_levelzero_device_pool.frequency_status(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MAX_AVAIL", {
                                  "Compute/GPU chip domain maximum available frequency in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.frequency_max(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MIN_AVAIL", {
                                  "Compute/GPU chip domain minimum available frequency in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.frequency_min(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MAX_CONTROL", {
                                  "Compute/GPU chip domain current maximum frequency requested in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return (this->m_levelzero_device_pool.frequency_range(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE)).second;
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MIN_CONTROL", {
                                  "Compute/GPU chip domain current minimum frequency requested in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return (this->m_levelzero_device_pool.frequency_range(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE)).first;
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPU_ENERGY", {
                                  "GPU energy in Joules",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.energy(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPU_ENERGY_TIMESTAMP", {
                                  "GPU energy timestamp in seconds"
                                  "\nValue cached on LEVELZERO::ENERGY read",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.energy_timestamp(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_MEMORY_FREQUENCY_STATUS", {
                                  "Compute/GPU chip domain memory current frequency in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.frequency_status(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_MEMORY);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_MEMORY_FREQUENCY_MAX_AVAIL", {
                                  "Compute/GPU chip domain memory maximum frequency available in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.frequency_max(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_MEMORY);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_MEMORY_FREQUENCY_MIN_AVAIL", {
                                  "Compute/GPU chip domain memory minimum frequency in hertz",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.frequency_min(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_MEMORY);
                                  },
                                  1e6
                                  }},
                              {M_NAME_PREFIX + "GPU_POWER_LIMIT_DEFAULT", {
                                  "Default power limit in Watts",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.power_limit_tdp(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e3
                                  }},
                              {M_NAME_PREFIX + "GPU_POWER_LIMIT_MIN_AVAIL", {
                                  "Minimum available power limit in Watts",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.power_limit_min(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e3
                                  }},
                              {M_NAME_PREFIX + "GPU_POWER_LIMIT_MAX_AVAIL", {
                                  "Maximum available power limit in Watts",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.power_limit_max(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e3
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME", {
                                  "Compute/GPU chip active time",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_TIMESTAMP", {
                                  "Compute/GPU chip active time reading timestamp"
                                  "\nValue cached on LEVELZERO::GPUCHIP_ACTIVE_TIME read",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time_timestamp(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_ALL);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COMPUTE", {
                                  "Compute/GPU chip domain compute engine active time",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COMPUTE_TIMESTAMP", {
                                  "Compute/GPU chip domain compute engine active time reading timestamp"
                                  "\nValue cached on LEVELZERO::GPUCHIP_ACTIVE_TIME_COMPUTE read",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time_timestamp(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_COMPUTE);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COPY", {
                                  "Compute/GPU chip domain copy engine active time",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_MEMORY);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COPY_TIMESTAMP", {
                                  "Compute/GPU chip domain copy engine active time timestamp"
                                  "\nValue cached on LEVELZERO::GPUCHIP_ACTIVE_TIME_COPY read",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      return this->m_levelzero_device_pool.active_time_timestamp(
                                                   GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                   domain_idx,
                                                   geopm::LevelZero::M_DOMAIN_MEMORY);
                                  },
                                  1 / 1e6
                                  }},
                              {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_CONTROL", {
                                  "Compute/GPU chip domain current requested frequency in hertz"
                                  "\nReadings are valid only after writing to this control",
                                  GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                  Agg::average,
                                  string_format_double,
                                  {},
                                  [this](unsigned int domain_idx) -> double
                                  {
                                      auto range_pair =  this->m_levelzero_device_pool.frequency_range(
                                                               GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                                               domain_idx,
                                                               geopm::LevelZero::M_DOMAIN_COMPUTE);
                                      return range_pair.first == range_pair.second ? range_pair.first
                                                              : NAN;
                                  },
                                  1e6
                                  }}
                             })
        , m_control_available({{M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MIN_CONTROL", {
                                    "Sets the compute/GPU chip domain frequency minimum in hertz",
                                    {},
                                    GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                    Agg::average,
                                    string_format_double
                                    }},
                               {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MAX_CONTROL", {
                                    "Sets the compute/GPU chip domain frequency maximum in hertz",
                                    {},
                                    GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                    Agg::average,
                                    string_format_double
                                    }},
                               {M_NAME_PREFIX + "GPUCHIP_FREQUENCY_CONTROL", {
                                    "Sets the compute/GPU chip fomain frequency both min and max in hertz",
                                    {},
                                    GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                    Agg::average,
                                    string_format_double
                                    }}
                              })
        , m_special_signal_set({M_NAME_PREFIX + "GPU_ENERGY",
                                M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME",
                                M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COMPUTE",
                                M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COPY"})

        , m_derivative_signal_map ({
            {M_NAME_PREFIX + "GPU_POWER",
                    {"Average GPU power over 40 ms or 8 control loop iterations",
                    M_NAME_PREFIX + "GPU_ENERGY",
                    M_NAME_PREFIX + "GPU_ENERGY_TIMESTAMP"}},
            {M_NAME_PREFIX + "GPU_UTILIZATION",
                    {"GPU utilization"
                        "n  Level Zero logical engines may map to the same hardware"
                        "\n  resulting in a reduced signal range (i.e. not 0 to 1)",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_TIMESTAMP"}},
            {M_NAME_PREFIX + "GPU_UTILIZATION_COMPUTE",
                    {"Compute engine utilization"
                        "n  Level Zero logical engines may map to the same hardware"
                        "\n  resulting in a reduced signal range (i.e. not 0 to 1)",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COMPUTE",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COMPUTE_TIMESTAMP"}},
            {M_NAME_PREFIX + "GPU_UTILIZATION_COPY",
                    {"Copy engine utilization"
                        "n  Level Zero logical engines may map to the same hardware"
                        "\n  resulting in a reduced signal range (i.e. not 0 to 1)",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COPY",
                    M_NAME_PREFIX + "GPUCHIP_ACTIVE_TIME_COPY_TIMESTAMP"}},
        })
        , m_mock_save_ctl(save_control)
    {
        // populate signals for each domain
        for (auto &sv : m_signal_available) {
            std::vector<std::shared_ptr<Signal> > result;
            for (int domain_idx = 0; domain_idx < m_platform_topo.num_domain(
                                     signal_domain_type(sv.first)); ++domain_idx) {
                std::shared_ptr<Signal> signal =
                        std::make_shared<LevelZeroSignal>(sv.second.m_devpool_func,
                                                          domain_idx,
                                                          sv.second.m_scalar);
                result.push_back(signal);
            }
            sv.second.m_signals = result;
        }

        register_derivative_signals();

        register_signal_alias("GPUCHIP_FREQUENCY_STATUS", M_NAME_PREFIX + "GPUCHIP_FREQUENCY_STATUS");
        register_signal_alias("GPU_POWER", M_NAME_PREFIX + "GPU_POWER");
        register_signal_alias("GPUCHIP_FREQUENCY_CONTROL",
                               M_NAME_PREFIX + "GPUCHIP_FREQUENCY_CONTROL");
        register_control_alias("GPUCHIP_FREQUENCY_CONTROL",
                               M_NAME_PREFIX + "GPUCHIP_FREQUENCY_CONTROL");

        // populate controls for each domain
        for (auto &sv : m_control_available) {
            std::vector<std::shared_ptr<control_s> > result;
            for (int domain_idx = 0;
                 domain_idx < m_platform_topo.num_domain(control_domain_type(sv.first));
                 ++domain_idx) {
                std::shared_ptr<control_s> ctrl = std::make_shared<control_s>(control_s{0, false});
                result.push_back(ctrl);
            }
            sv.second.m_controls = result;
        }
    }

    void LevelZeroIOGroup::register_derivative_signals(void) {
        int derivative_window = 8;
        double sleep_time = 0.005;

        for (const auto &ds : m_derivative_signal_map) {
            auto read_it = m_signal_available.find(ds.second.m_base_name);
            auto time_it = m_signal_available.find(ds.second.m_time_name);
            if (read_it != m_signal_available.end()
                && time_it != m_signal_available.end()) {
                auto readings = read_it->second.m_signals;
                int domain = read_it->second.m_domain_type;
                int num_domain = m_platform_topo.num_domain(domain);
                GEOPM_DEBUG_ASSERT(num_domain == (int)readings.size(),
                                   "size of domain for " + ds.second.m_base_name +
                                   " does not match number of signals available.");

                auto time_sig = time_it->second.m_signals;
                GEOPM_DEBUG_ASSERT(time_it->second.m_domain_type == domain,
                                   "domain for " + ds.second.m_time_name +
                                   " does not match " + ds.second.m_base_name);

                std::vector<std::shared_ptr<Signal> > result(num_domain);
                for (int domain_idx = 0; domain_idx < num_domain; ++domain_idx) {
                    auto read = readings[domain_idx];
                    auto time = time_sig[domain_idx];
                    result[domain_idx] =
                        std::make_shared<DerivativeSignal>(time, read,
                                                           derivative_window,
                                                           sleep_time);
                }
                m_signal_available[ds.first] = {ds.second.m_description + "\n    alias_for: " +
                                                ds.second.m_base_name + " rate of change",
                                                domain,
                                                agg_function(ds.second.m_base_name),
                                                format_function(ds.second.m_base_name),
                                                result,
                                                nullptr,
                                                1};

            }
        }
    }

    // Extract the set of all signal names from the index map
    std::set<std::string> LevelZeroIOGroup::signal_names(void) const
    {
        std::set<std::string> result;
        for (const auto &sv : m_signal_available) {
            result.insert(sv.first);
        }
        return result;
    }

    // Extract the set of all control names from the index map
    std::set<std::string> LevelZeroIOGroup::control_names(void) const
    {
        std::set<std::string> result;
        for (const auto &sv : m_control_available) {
            result.insert(sv.first);
        }
        return result;
    }

    // Check signal name using index map
    bool LevelZeroIOGroup::is_valid_signal(const std::string &signal_name) const
    {
        return m_signal_available.find(signal_name) != m_signal_available.end();
    }

    // Check control name using index map
    bool LevelZeroIOGroup::is_valid_control(const std::string &control_name) const
    {
        return m_control_available.find(control_name) != m_control_available.end();
    }

    // Return domain for all valid signals
    int LevelZeroIOGroup::signal_domain_type(const std::string &signal_name) const
    {
        int result = GEOPM_DOMAIN_INVALID;
        auto it = m_signal_available.find(signal_name);
        if (it != m_signal_available.end()) {
            result = it->second.m_domain_type;
        }
        return result;
    }

    // Return domain for all valid controls
    int LevelZeroIOGroup::control_domain_type(const std::string &control_name) const
    {
        int result = GEOPM_DOMAIN_INVALID;
        auto it = m_control_available.find(control_name);
        if (it != m_control_available.end()) {
            result = it->second.m_domain_type;
        }
        return result;
    }

    // Mark the given signal to be read by read_batch()
    int LevelZeroIOGroup::push_signal(const std::string &signal_name,
                                      int domain_type, int domain_idx)
    {
        if (!is_valid_signal(signal_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": signal_name " + signal_name +
                            " not valid for LevelZeroIOGroup.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_type != signal_domain_type(signal_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": " + signal_name + ": domain_type must be " +
                            std::to_string(signal_domain_type(signal_name)),
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_idx < 0 ||
            domain_idx >= m_platform_topo.num_domain(
                                          signal_domain_type(signal_name))) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": domain_idx out of range.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (m_is_batch_read) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": cannot push signal after call to read_batch().",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }

        int result = -1;
        bool is_found = false;

        // Guarantee base signal was pushed before any timestamp signals
        if (string_ends_with(signal_name, "_TIMESTAMP")) {
            std::string base_signal_name =
                (std::string)signal_name.substr(
                0, signal_name.length() - strlen("_TIMESTAMP"));

            std::shared_ptr<Signal> base_signal = m_signal_available.at(
                base_signal_name).m_signals.at(domain_idx);

            // check if base signal was pushed
            for (size_t ii = 0; !is_found && ii < m_signal_pushed.size(); ++ii) {
                if (m_signal_pushed[ii] == base_signal) {
                    result = ii;
                    is_found = true;
                }
            }
            // if not, push the base signal
            if (!is_found) {
                push_signal(base_signal_name, domain_type, domain_idx);
            }
        }

        // Reset is_found for next search
        is_found = false;

        std::shared_ptr<Signal> signal = m_signal_available.at(
                                         signal_name).m_signals.at(domain_idx);

        // Check if signal was already pushed.
        for (size_t ii = 0; !is_found && ii < m_signal_pushed.size(); ++ii) {
            if (m_signal_pushed[ii] == signal) {
                result = ii;
                is_found = true;
            }
        }
        if (!is_found) {
            // If not pushed, add to pushed signals and configure for batch reads
            result = m_signal_pushed.size();
            m_signal_pushed.push_back(signal);
            signal->setup_batch();

            if (m_special_signal_set.find(signal_name) != m_special_signal_set.end()) {
                push_signal(signal_name + "_TIMESTAMP", domain_type, domain_idx);
            }
        }

        // Push signals related to derivative signals
        auto derivative_it = m_derivative_signal_map.find(signal_name);
        if (derivative_it != m_derivative_signal_map.end()) {
            //Add derivative signals to the skip list
            m_derivative_signal_pushed_set.insert(result);

            //push associated signals
            push_signal(derivative_it->second.m_base_name, domain_type, domain_idx);
            push_signal(derivative_it->second.m_time_name, domain_type, domain_idx);
        }

        return result;
    }

    // Mark the given control to be written by write_batch()
    int LevelZeroIOGroup::push_control(const std::string &control_name,
                                       int domain_type, int domain_idx)
    {
        if (!is_valid_control(control_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": control_name " + control_name +
                            " not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_type != control_domain_type(control_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": " + control_name + ": domain_type must be " +
                            std::to_string(control_domain_type(control_name)),
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_idx < 0 ||
            domain_idx >= m_platform_topo.num_domain(domain_type)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": domain_idx out of range.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }

        int result = -1;
        bool is_found = false;
        std::shared_ptr<control_s> control = m_control_available.at(
                                             control_name).m_controls.at(domain_idx);

        // Check if control was already pushed
        for (size_t ii = 0; !is_found && ii < m_control_pushed.size(); ++ii) {
            // same location means this control or its alias was already pushed
            if (m_control_pushed[ii] == control) {
                result = ii;
                is_found = true;
            }
        }
        if (!is_found) {
            // If not pushed, add to pushed control
            result = m_control_pushed.size();
            m_control_pushed.push_back(control);
        }

        return result;
    }

    // Parse and update saved values for signals
    void LevelZeroIOGroup::read_batch(void)
    {
        m_is_batch_read = true;
        for (size_t ii = 0; ii < m_signal_pushed.size(); ++ii) {
            if (m_derivative_signal_pushed_set.find(ii) == m_derivative_signal_pushed_set.end()) {
                m_signal_pushed[ii]->set_sample(m_signal_pushed[ii]->read());
            }
        }
    }

    // Write all controls that have been pushed and adjusted
    void LevelZeroIOGroup::write_batch(void)
    {
        for (auto &sv : m_control_available) {
            for (unsigned int domain_idx = 0;
                 domain_idx < sv.second.m_controls.size(); ++domain_idx) {
                if (sv.second.m_controls.at(domain_idx)->m_is_adjusted) {
                    write_control(sv.first, sv.second.m_domain_type, domain_idx,
                                  sv.second.m_controls.at(domain_idx)->m_setting);
                }
            }
        }
    }

    // Return the latest value read by read_batch()
    double LevelZeroIOGroup::sample(int batch_idx)
    {
        if (batch_idx < 0 || batch_idx >= (int)m_signal_pushed.size()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": batch_idx " + std::to_string(batch_idx) +
                            " out of range", GEOPM_ERROR_INVALID,
                            __FILE__, __LINE__);

        }
        //Not strictly necessary, but keeping to enforce general
        //flow of read_batch followed by sample.
        if (!m_is_batch_read) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": signal has not been read.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }

        return m_signal_pushed[batch_idx]->sample();
    }

    // Save a setting to be written by a future write_batch()
    void LevelZeroIOGroup::adjust(int batch_idx, double setting)
    {
        if (batch_idx < 0 || (unsigned)batch_idx >= m_control_pushed.size()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            "(): batch_idx " + std::to_string(batch_idx) +
                            " out of range", GEOPM_ERROR_INVALID,
                            __FILE__, __LINE__);
        }

        m_control_pushed.at(batch_idx)->m_setting = setting;
        m_control_pushed.at(batch_idx)->m_is_adjusted = true;
    }

    // Read the value of a signal immediately, bypassing read_batch().
    // Should not modify m_signal_value
    double LevelZeroIOGroup::read_signal(const std::string &signal_name,
                                         int domain_type, int domain_idx)
    {
        if (!is_valid_signal(signal_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": " + signal_name + " not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_type != signal_domain_type(signal_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                            + signal_name + ": domain_type must be " +
                            std::to_string(signal_domain_type(signal_name)),
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_idx < 0 ||
            domain_idx >= m_platform_topo.num_domain(signal_domain_type(signal_name))) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": domain_idx out of range.", GEOPM_ERROR_INVALID,
                            __FILE__, __LINE__);
        }
        if (string_ends_with(signal_name, "_TIMESTAMP")) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": TIMESTAMP Signals are for batch use only.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        double result = NAN;
        auto it = m_signal_available.find(signal_name);
        if (it != m_signal_available.end()) {
            result = (it->second.m_signals.at(domain_idx))->read();
        }
        else {
    #ifdef GEOPM_DEBUG
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": Handling not defined for " + signal_name,
                            GEOPM_ERROR_LOGIC, __FILE__, __LINE__);
    #endif
        }
        return result;
    }

    // Write to the control immediately, bypassing write_batch()
    void LevelZeroIOGroup::write_control(const std::string &control_name,
                                         int domain_type, int domain_idx,
                                         double setting)
    {
        if (!is_valid_control(control_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                            + control_name + " not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_type != control_domain_type(control_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                            + control_name + ": domain_type must be " +
                            std::to_string(control_domain_type(control_name)),
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        if (domain_idx < 0 ||
            domain_idx >= m_platform_topo.num_domain(
                                          control_domain_type(control_name))) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": domain_idx out of range.", GEOPM_ERROR_INVALID,
                            __FILE__, __LINE__);
        }

        if (control_name == M_NAME_PREFIX + "GPUCHIP_FREQUENCY_CONTROL" ||
            control_name == "GPUCHIP_FREQUENCY_CONTROL") {
            m_levelzero_device_pool.frequency_control(domain_type, domain_idx,
                                                      geopm::LevelZero::M_DOMAIN_COMPUTE,
                                                      setting / 1e6, setting / 1e6);
        }
        else if(control_name == M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MIN_CONTROL") {
            double curr_max = read_signal(M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MAX_CONTROL",
                                          domain_type, domain_idx);
            m_levelzero_device_pool.frequency_control(domain_type, domain_idx,
                                                      geopm::LevelZero::M_DOMAIN_COMPUTE,
                                                      setting / 1e6, curr_max / 1e6);
        }
        else if(control_name == M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MAX_CONTROL") {
            double curr_min = read_signal(M_NAME_PREFIX + "GPUCHIP_FREQUENCY_MIN_CONTROL",
                                          domain_type, domain_idx);
            m_levelzero_device_pool.frequency_control(domain_type, domain_idx,
                                                      geopm::LevelZero::M_DOMAIN_COMPUTE,
                                                      curr_min / 1e6, setting / 1e6);
        }
        else {
    #ifdef GEOPM_DEBUG
                throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                                "Handling not defined for " + control_name,
                                GEOPM_ERROR_LOGIC, __FILE__, __LINE__);
    #endif
        }
    }

    // Implemented to allow an IOGroup to save platform settings before starting
    // to adjust them
    void LevelZeroIOGroup::save_control(void)
    {
        for (int domain_idx = 0;
             domain_idx < m_platform_topo.num_domain(GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP);
             ++domain_idx) {

            try {
                // Currently only the levelzero compute domain control is supported.
                // As new controls are added they should be included
                m_frequency_range.push_back(m_levelzero_device_pool.frequency_range(
                                            GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP, domain_idx,
                                            geopm::LevelZero::M_DOMAIN_COMPUTE));
            }
            catch (const geopm::Exception &ex) {
                throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                                + " Failed to fetch frequency control range for "
                                " BOARD_ACCELERATOR_CHIP domain " + std::to_string(domain_idx),
                                GEOPM_ERROR_INVALID, __FILE__, __LINE__);
            }
        }
    }

    // Implemented to allow an IOGroup to restore previously saved
    // platform settings
    void LevelZeroIOGroup::restore_control(void)
    {
        for (int domain_idx = 0;
             domain_idx < m_platform_topo.num_domain(GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP);
             ++domain_idx) {
            try {
                // Currently only the levelzero compute domain control is supported.
                // As new controls are added they should be included
                m_levelzero_device_pool.frequency_control(GEOPM_DOMAIN_BOARD_ACCELERATOR_CHIP,
                                        domain_idx, geopm::LevelZero::M_DOMAIN_COMPUTE,
                                        m_frequency_range.at(domain_idx).first,
                                        m_frequency_range.at(domain_idx).second);
            }
            catch (const geopm::Exception &ex) {
#ifdef GEOPM_DEBUG
                std::cerr << "Warning: <geopm> LevelZeroIOGroup: Failed to "
                             "restore frequency control settings for "
                             "BOARD_ACCELERATOR_CHIP domain " << std::to_string(domain_idx)
                             << ".  Exception: " << ex.what()
                             << std::endl;
#endif
            }
        }
    }

    // Hint to Agent about how to aggregate signals from this IOGroup
    std::function<double(const std::vector<double> &)> LevelZeroIOGroup::agg_function(const std::string &signal_name) const
    {
        auto it = m_signal_available.find(signal_name);
        if (it == m_signal_available.end()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                            + signal_name + "not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        return it->second.m_agg_function;
    }

    // Specifies how to print signals from this IOGroup
    std::function<std::string(double)> LevelZeroIOGroup::format_function(const std::string &signal_name) const
    {
        auto it = m_signal_available.find(signal_name);
        if (it == m_signal_available.end()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": "
                            + signal_name + "not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        return it->second.m_format_function;
    }

    // A user-friendly description of each signal
    std::string LevelZeroIOGroup::signal_description(const std::string &signal_name) const
    {
        if (!is_valid_signal(signal_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": signal_name " + signal_name +
                            " not valid for LevelZeroIOGroup.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        return m_signal_available.at(signal_name).m_description;
    }

    // A user-friendly description of each control
    std::string LevelZeroIOGroup::control_description(const std::string &control_name) const
    {
        if (!is_valid_control(control_name)) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) + ": " +
                            control_name + "not valid for LevelZeroIOGroup",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }

        return m_control_available.at(control_name).m_description;
    }

    std::string LevelZeroIOGroup::name(void) const
    {
        return plugin_name();
    }

    // Name used for registration with the IOGroup factory
    std::string LevelZeroIOGroup::plugin_name(void)
    {
        return M_PLUGIN_NAME;
    }

    int LevelZeroIOGroup::signal_behavior(const std::string &signal_name) const
    {
        return IOGroup::M_SIGNAL_BEHAVIOR_VARIABLE;
    }

    // Function used by the factory to create objects of this type
    std::unique_ptr<IOGroup> LevelZeroIOGroup::make_plugin(void)
    {
        return geopm::make_unique<LevelZeroIOGroup>();
    }

    void LevelZeroIOGroup::register_signal_alias(const std::string &alias_name,
                                                 const std::string &signal_name)
    {
        if (m_signal_available.find(alias_name) != m_signal_available.end()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": signal_name " + alias_name +
                            " was previously registered.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        auto it = m_signal_available.find(signal_name);
        if (it == m_signal_available.end()) {
            // skip adding an alias if underlying signal is not found
            return;
        }
        // copy signal info but append to description
        m_signal_available[alias_name] = it->second;
        m_signal_available[alias_name].m_description =
            m_signal_available[signal_name].m_description +
            "\n    alias_for: " + signal_name;
    }

    void LevelZeroIOGroup::register_control_alias(const std::string &alias_name,
                                                  const std::string &control_name)
    {
        if (m_control_available.find(alias_name) != m_control_available.end()) {
            throw Exception("LevelZeroIOGroup::" + std::string(__func__) +
                            ": contro1_name " + alias_name +
                            " was previously registered.",
                            GEOPM_ERROR_INVALID, __FILE__, __LINE__);
        }
        auto it = m_control_available.find(control_name);
        if (it == m_control_available.end()) {
            // skip adding an alias if underlying control is not found
            return;
        }
        // copy control info but append to description
        m_control_available[alias_name] = it->second;
        m_control_available[alias_name].m_description =
            m_control_available[control_name].m_description +
            "\n    alias_for: " + control_name;
    }

    void LevelZeroIOGroup::save_control(const std::string &save_path)
    {
        std::shared_ptr<SaveControl> save_ctl = m_mock_save_ctl;
        if (save_ctl == nullptr) {
            save_ctl = SaveControl::make_unique(*this);
        }
        save_ctl->write_json(save_path);
    }

    void LevelZeroIOGroup::restore_control(const std::string &save_path)
    {
        std::shared_ptr<SaveControl> save_ctl = m_mock_save_ctl;
        if (save_ctl == nullptr) {
            save_ctl = SaveControl::make_unique(geopm::read_file(save_path));
        }
        save_ctl->restore(*this);
    }

}
