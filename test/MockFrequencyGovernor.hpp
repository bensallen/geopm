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

#ifndef MOCKFREQUENCYGOVERNOR_HPP_INCLUDE
#define MOCKFREQUENCYGOVERNOR_HPP_INCLUDE

#include "gmock/gmock.h"

#include "FrequencyGovernor.hpp"

class MockFrequencyGovernor : public geopm::FrequencyGovernor
{
    public:
        MOCK_METHOD(void, init_platform_io, (), (override));
        MOCK_METHOD(int, frequency_domain_type, (), (const, override));
        MOCK_METHOD(void, adjust_platform,
                    (const std::vector<double> &frequency_request), (override));
        MOCK_METHOD(bool, do_write_batch, (), (const, override));
        MOCK_METHOD(bool, set_frequency_bounds,
                    (double freq_min, double freq_max), (override));
        MOCK_METHOD(double, get_frequency_min, (), (const, override));
        MOCK_METHOD(double, get_frequency_max, (), (const, override));
        MOCK_METHOD(double, get_frequency_step, (), (const, override));
        MOCK_METHOD(void, validate_policy, (double &freq_min, double &freq_max),
                    (const, override));
};

#endif
