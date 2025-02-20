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

#ifndef POLICYSTOREIMP_HPP_INCLUDE
#define POLICYSTOREIMP_HPP_INCLUDE

#include <string>
#include <vector>

#include "PolicyStore.hpp"

struct sqlite3;

namespace geopm
{
    /// Manages a data store of best known policies for profiles used with
    /// agents. The data store includes records of best known policies and
    /// default policies to apply when a best run has not yet been recorded.
    class PolicyStoreImp : public PolicyStore
    {
        public:
            PolicyStoreImp(const std::string &database_path);

            PolicyStoreImp() = delete;
            PolicyStoreImp(const PolicyStoreImp &other) = delete;
            PolicyStoreImp &operator=(const PolicyStoreImp &other) = delete;

            virtual ~PolicyStoreImp();

            std::vector<double> get_best(const std::string &agent_name,
                                         const std::string &profile_name) const override;

            void set_best(const std::string &agent_name,
                          const std::string &profile_name,
                          const std::vector<double> &policy) override;

            void set_default(const std::string &agent_name, const std::vector<double> &policy) override;

        private:
            struct sqlite3 *m_database;
    };
}

#endif
