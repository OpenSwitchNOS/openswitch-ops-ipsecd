/*
 *Copyright (C) 2016 Hewlett-Packard Development Company, L.P.
 *All Rights Reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License"); you may
 *   not use this file except in compliance with the License. You may obtain
 *   a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *   License for the specific language governing permissions and limitations
 *   under the License.
 */

/**********************************
* System Includes
**********************************/
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

/**********************************
* Local Includes
***********************************/
#include "IPsecOvsdb.h"
#include "IIPsecOvsdbIDLWrapper.h"
#include "ops_ipsecd_helper.h"

IPsecOvsdb::IPsecOvsdb(IIPsecOvsdbIDLWrapper& idl_wrapper)
    : m_idl_wrapper(idl_wrapper)
{
}

IPsecOvsdb::~IPsecOvsdb()
{
    m_idl_wrapper.idl_destroy(m_idl);
}

ipsec_ret IPsecOvsdb::initialize()
{
    ipsec_ret result = ipsec_ret::OK;
    std::string run_path(m_idl_wrapper.rundir());

    m_idl_wrapper.init();

    /*set the ovsdb path*/
    set_db_path("unix:" + run_path + "/db.sock");

    /* Initialize IDL through a new connection to the DB*/
    m_idl = m_idl_wrapper.idl_create(
            db_path.c_str(), &ovsrec_idl_class, false, true);
    m_idl_seqno = m_idl_wrapper.idl_get_seqno(m_idl);
    m_idl_wrapper.idl_set_lock(m_idl, "ops_ipsecd");


    /* Cache System table for ipsec_manual_sa table*/
    m_idl_wrapper.idl_add_table(m_idl, &ovsrec_table_ipsec_manual_sa);
    m_idl_wrapper.idl_add_and_track_all_column(
            m_idl, &ovsrec_table_ipsec_manual_sa);

    /* Cache System table for ipsec_manual_sp table*/
    m_idl_wrapper.idl_add_table(m_idl, &ovsrec_table_ipsec_manual_sp);
    m_idl_wrapper.idl_add_and_track_all_column(
            m_idl, &ovsrec_table_ipsec_manual_sp);

    /* Cache System table for ipsec_ike_policy table*/
    m_idl_wrapper.idl_add_table(m_idl, &ovsrec_table_ipsec_ike_policy);
    m_idl_wrapper.idl_add_and_track_all_column(
            m_idl, &ovsrec_table_ipsec_ike_policy);

    /*Add omit alerts here*/

    return result;
}

ipsec_ret IPsecOvsdb::run()
{
    m_idl_seqno_new = m_idl_wrapper.idl_get_seqno(m_idl);

    /* Process a batch of messages from OVSDB. */
    m_idl_wrapper.idl_run(m_idl);

    m_idl_seqno = m_idl_wrapper.idl_get_seqno(m_idl);

    if(m_idl_seqno != m_idl_seqno_new)
    {
        /*Some change happen into the OVSDB*/
        if (m_is_ready)
        {
            update_cache(m_idl_seqno_new);
        }
        else
        {
            update_cache(m_idl_seqno);
            m_is_ready = true;
            /*TODO: add log info*/
            printf("Updating ops-ipsecd tables cache from OVSDB server.... done \n");
        }
    }

    if(m_idl_wrapper.idl_is_lock_contended(m_idl))
    {
        //TODO: add log
        printf("Another ipsecd process is running \n");
        return ipsec_ret::NOT_RUNNING;
    }
    if(!m_idl_wrapper.idl_has_lock(m_idl))
    {
        /*Add log info*/
        printf("no lock....\n");
        return ipsec_ret::NOT_READY;
    }

    return ipsec_ret::IS_RUNNING;
}

void IPsecOvsdb::wait()
{
    m_idl_wrapper.idl_wait(m_idl);
}

ipsec_ret IPsecOvsdb::ipsec_manual_sa_modify_row(const ipsec_sa& sa)
{
    ipsec_ret result = ipsec_ret::NOT_FOUND;
    const struct ovsrec_ipsec_manual_sa *sa_row = nullptr;
    enum ovsdb_idl_txn_status status;
    idl_txn_t status_txn;

   /* New transaction to add a new row*/
    status_txn = m_idl_wrapper.idl_txn_create(m_idl);

    sa_row = m_idl_wrapper.ipsec_manual_sa_first(m_idl);
    if(sa_row != nullptr)
    {
        OVSREC_IPSEC_MANUAL_SA_FOR_EACH(sa_row, m_idl)
        {
            /*SA is going to be modified*/
            if(sa_row->SPI == sa.m_id.m_spi)
            {
                set_integer_to_column(const_cast<idl_row_t>(&sa_row->header_),
                        &ovsrec_ipsec_manual_sa_col_request_id, sa.m_req_id);

                status = m_idl_wrapper.idl_txn_commit_block(status_txn);
                if(status != TXN_SUCCESS && status != TXN_UNCHANGED)
                {
                    //TODO: add log
                    printf("Updating SA failed \n");
                }
                else
                {
                    //TODO: add log
                    printf("Updating, success\n\n");
                    result = ipsec_ret::OK;
                }

                m_idl_wrapper.idl_txn_destroy(status_txn);
                printf("%s\n",ovsdb_idl_txn_status_to_string(status));
                return result;
            }
        }
    }

    m_idl_wrapper.idl_txn_destroy(status_txn);
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sa_insert_row(const ipsec_sa& sa)
{
    ipsec_ret result = ipsec_ret::ADD_FAILED;
    const struct ovsrec_ipsec_manual_sa *sa_row = nullptr;
    enum ovsdb_idl_txn_status status;
    idl_txn_t status_txn;

    /*New transaction to add a new row*/
    status_txn = m_idl_wrapper.idl_txn_create(m_idl);

    sa_row = m_idl_wrapper.ipsec_manual_sa_first(m_idl);
    if(sa_row != nullptr)
    {
        OVSREC_IPSEC_MANUAL_SA_FOR_EACH(sa_row, m_idl)
        {
            /*SA already exist, nothing to do*/
            if(sa_row->SPI == sa.m_id.m_spi)
            {
                m_idl_wrapper.idl_txn_destroy(status_txn);
                return result;
            }
        }
    }

    /*Get a New row from the OVSDB*/
    sa_row = m_idl_wrapper.ipsec_manual_sa_insert(status_txn);

    /*TODO: replace this with a template design*/
    ovsrec_ipsec_manual_sa_set_SPI(sa_row, sa.m_id.m_spi);

    status = m_idl_wrapper.idl_txn_commit_block(status_txn);

    printf("%s\n",ovsdb_idl_txn_status_to_string(status));
    if(status != TXN_SUCCESS && status != TXN_UNCHANGED)
    {
        //TODO: add log
        printf("Creating new SA failed \n");
    }
    else
    {
        //TODO: add log
        printf("Creating new SA, success\n\n");
        result = ipsec_ret::OK;
    }

    m_idl_wrapper.idl_txn_destroy(status_txn);
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sa_get_row(
        int64_t spi, ipsec_sa& sa)
{
    const struct ovsrec_ipsec_manual_sa *sa_row = nullptr;

    sa_row = m_idl_wrapper.ipsec_manual_sa_first(m_idl);
    if(sa_row != nullptr)
    {
        OVSREC_IPSEC_MANUAL_SA_FOR_EACH(sa_row, m_idl)
        {
            if(sa_row->SPI == spi)
            {
                ovsrec_to_ipsec_sa(const_cast<ipsec_manual_sa_t>(sa_row), sa);
                return ipsec_ret::OK;
            }
        }
    }

    return ipsec_ret::NOT_FOUND;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sa_delete_row(const ipsec_sa& sa)
{
    ipsec_ret result = ipsec_ret::DELETE_FAILED;
    const struct ovsrec_ipsec_manual_sa *sa_row = nullptr;
    enum ovsdb_idl_txn_status status;
    idl_txn_t status_txn;

   /* New transaction to add a new row*/
    status_txn = m_idl_wrapper.idl_txn_create(m_idl);

    sa_row = m_idl_wrapper.ipsec_manual_sa_first(m_idl);
    if(sa_row != nullptr)
    {
        OVSREC_IPSEC_MANUAL_SA_FOR_EACH(sa_row, m_idl)
        {
            /*SA must be deleted*/
            if(sa_row->SPI == sa.m_id.m_spi)
            {
                m_idl_wrapper.idl_txn_delete(const_cast<idl_row_t>(&sa_row->header_));

                status = m_idl_wrapper.idl_txn_commit_block(status_txn);
                if(status != TXN_SUCCESS && status != TXN_UNCHANGED)
                {
                    //TODO: add log
                    printf("Deleting SA failed \n");
                }
                else
                {
                    //TODO: add log
                    printf("Deleting, success\n\n");
                    result = ipsec_ret::OK;
                }

                m_idl_wrapper.idl_txn_destroy(status_txn);
                printf("%s\n",ovsdb_idl_txn_status_to_string(status));
                return result;
            }
        }
    }

    m_idl_wrapper.idl_txn_destroy(status_txn);
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sp_insert_row(const ipsec_sp& sp)
{
    ipsec_ret result = ipsec_ret::ADD_FAILED;
    const struct ovsrec_ipsec_manual_sp *sp_row = nullptr;
    enum ovsdb_idl_txn_status status;
    idl_txn_t status_txn;
    std::string dst_ip="";
    std::string src_ip="";

    /*New transaction to add a new row*/
    status_txn = m_idl_wrapper.idl_txn_create(m_idl);

    sp_row = m_idl_wrapper.ipsec_manual_sp_first(m_idl);
    if(sp_row != nullptr)
    {
        ipsecd_helper::get_dst_selector(sp.m_id.m_selector, dst_ip);
        ipsecd_helper::get_src_selector(sp.m_id.m_selector, src_ip);
        OVSREC_IPSEC_MANUAL_SP_FOR_EACH(sp_row, m_idl)
        {
            if(strcmp(sp_row->direction,
                        ipsecd_helper::direction_to_str(sp.m_id.m_dir))==0 &&
                    strcmp(sp_row->src_prefix, src_ip.c_str())==0 &&
                    strcmp(sp_row->dest_prefix, dst_ip.c_str())==0)
            {
                m_idl_wrapper.idl_txn_destroy(status_txn);
                return result;
            }
        }
    }

    /*Get a New row from the OVSDB*/
    sp_row = m_idl_wrapper.ipsec_manual_sp_insert(status_txn);

    /*TODO: replace this with a template design*/
    ovsrec_ipsec_manual_sp_set_src_prefix(sp_row, "172.1.1.1");
    ovsrec_ipsec_manual_sp_set_dest_prefix(sp_row, "172.1.1.2");
    ovsrec_ipsec_manual_sp_set_direction(sp_row, "in");

    status = m_idl_wrapper.idl_txn_commit_block(status_txn);

    printf("%s\n",ovsdb_idl_txn_status_to_string(status));
    if(status != TXN_SUCCESS && status != TXN_UNCHANGED)
    {
        //TODO: add log
        printf("Creating new SP failed \n");
    }
    else
    {
        //TODO: add log
        printf("Creating new SP, success\n\n");
        result = ipsec_ret::OK;
    }

    m_idl_wrapper.idl_txn_destroy(status_txn);
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sp_delete_row(ipsec_direction dir,
                const ipsec_selector& selector)
{
    ipsec_ret result = ipsec_ret::NOT_FOUND;
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sp_get_row(ipsec_direction dir,
                const ipsec_selector& selector, ipsec_sp& sp)
{
    ipsec_ret result = ipsec_ret::NOT_FOUND;
    const struct ovsrec_ipsec_manual_sp *sp_row = nullptr;
    std::string dst_ip="";
    std::string src_ip="";


    sp_row = m_idl_wrapper.ipsec_manual_sp_first(m_idl);
    if(sp_row != nullptr)
    {
        ipsecd_helper::get_dst_selector(sp.m_id.m_selector, dst_ip);
        ipsecd_helper::get_src_selector(sp.m_id.m_selector, src_ip);
        OVSREC_IPSEC_MANUAL_SP_FOR_EACH(sp_row, m_idl)
        {
            if(strcmp(sp_row->direction,
                        ipsecd_helper::direction_to_str(sp.m_id.m_dir))==0 &&
                    strcmp(sp_row->src_prefix, src_ip.c_str())==0 &&
                    strcmp(sp_row->dest_prefix, dst_ip.c_str())==0)
            {
                ovsrec_to_ipsec_sp(const_cast<ipsec_manual_sp_t>(sp_row), sp);
                result = ipsec_ret::OK;
                return result;
            }
        }
    }
    return result;
}

ipsec_ret IPsecOvsdb::ipsec_manual_sp_modify_row(const ipsec_sp& sp)
{
    ipsec_ret result = ipsec_ret::ADD_FAILED;
    return result;
}

ipsec_ret IPsecOvsdb::update_cache(unsigned int seq_no)
{
    ipsec_ret result = ipsec_ret::OK;
    const struct ovsrec_ipsec_manual_sa *sa_row = nullptr;
    const struct ovsrec_ipsec_manual_sp *sp_row = nullptr;
    const struct ovsrec_ipsec_ike_policy *policy_row = nullptr;


    /* Check for changes on the SA table*/
    sa_row = m_idl_wrapper.ipsec_manual_sa_track_get_first(m_idl);
    if(sa_row != nullptr)
    {
        /*New changes*/
        OVSREC_IPSEC_MANUAL_SA_FOR_EACH_TRACKED(sa_row, m_idl)
        {
            /* Check for changes to row. */
            if (OVSREC_IDL_IS_ROW_INSERTED(sa_row, seq_no))
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sa_created) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
            else if(OVSREC_IDL_IS_ROW_MODIFIED(sa_row, seq_no))
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sa_modified) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                    ipsec_sa sa;
                    ovsrec_to_ipsec_sa(const_cast<ipsec_manual_sa_t>(sa_row), sa);
                }
            }
            else if(m_is_ready == true)
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sa_deleted) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
        }
    }

    /* Check for changes on the SP table*/
    sp_row = m_idl_wrapper.ipsec_manual_sp_track_get_first(m_idl);
    if(sp_row != nullptr)
    {
        /*New changes*/
        OVSREC_IPSEC_MANUAL_SP_FOR_EACH_TRACKED(sp_row, m_idl)
        {
            /* Check for changes to row. */
            if (OVSREC_IDL_IS_ROW_INSERTED(sp_row, seq_no))
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sp_created) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
            else if(OVSREC_IDL_IS_ROW_MODIFIED(sp_row, seq_no))
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sp_modified) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
            else if(m_is_ready == true)
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::sp_deleted) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
        }
    }

    /* Check for changes on the IKE_Policy table*/
    policy_row = m_idl_wrapper.ipsec_ike_policy_track_get_first(m_idl);
    if(policy_row != nullptr)
    {
        /*New changes*/
        OVSREC_IPSEC_IKE_POLICY_FOR_EACH_TRACKED(policy_row, m_idl)
        {
            /* Check for changes to row. */
            if (OVSREC_IDL_IS_ROW_INSERTED(policy_row, seq_no))
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::ike_policy_created) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
            else if(OVSREC_IDL_IS_ROW_MODIFIED(sa_row, seq_no))
            {
                if (ipsecd_ovsdb_event(ipsec_events::ike_policy_modified) \
                        == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
            else if(m_is_ready == true)
            {
                if (ipsecd_ovsdb_event(
                            ipsec_events::ike_policy_deleted) == ipsec_ret::OK)
                {
                    /*TODO: add log info*/
                }
            }
        }
    }
    /*flush information about tracked columns*/
    m_idl_wrapper.idl_track_clear(m_idl);
    return result;
}

ipsec_ret IPsecOvsdb::ipsecd_ovsdb_event(ipsec_events event)
{
    ipsec_ret result = ipsec_ret::OK;
    switch(event)
    {
        case ipsec_events::sa_created:
            printf("... SA created\n");
            break;
        case ipsec_events::sa_deleted:
            printf("... SA deleted\n");
            break;
        case ipsec_events::sa_modified:
            printf("... SA modified\n");
            break;
        case ipsec_events::sp_created:
            printf("... SP created\n");
            break;
        case ipsec_events::sp_deleted:
            printf("... SP deleted\n");
            break;
        case ipsec_events::sp_modified:
            printf("... SP modified\n");
            break;
        case ipsec_events::ike_policy_created:
            printf("... IKE Policy created\n");
            break;
        case ipsec_events::ike_policy_deleted:
            printf("... IKE Policy deleted\n");
            break;
        case ipsec_events::ike_policy_modified:
            printf("... IKE Policy modified\n");
            break;
        default:
            return ipsec_ret::ERR;
    }
    return result;
}

void IPsecOvsdb::ovsrec_to_ipsec_sa(const ipsec_manual_sa_t row,
        ipsec_sa& sa)
{
    std::string column = "";

    sa.m_id.m_spi = (uint32_t)row->SPI;
    if(row->mode != nullptr)
    {
        column.assign(row->mode);
        if (column.compare(OVSREC_IPSEC_MANUAL_SA_MODE_TRANSPORT))
        {
            sa.m_mode = ipsec_mode::transport;
        }
        else
        {
            sa.m_mode = ipsec_mode::tunnel;
        }
    }
    sa.m_req_id = (uint32_t)row->request_id;
    if(row->src_ip != nullptr)
    {
        column.assign(row->src_ip);
        if(column.find_first_of(":")==std::string::npos)
        {
            /*IPv4*/
            sa.m_id.m_addr_family = AF_INET;
        }
        else
        {
            /*IPv6*/
            sa.m_id.m_addr_family = AF_INET6;
        }
        ipsecd_helper::set_str_to_ip_addr_t(column, sa.m_id.m_addr_family,
                sa.m_id.m_src_ip);
    }
    if(row->dest_ip != nullptr)
    {
        column.assign(row->dest_ip);
        ipsecd_helper::set_str_to_ip_addr_t(column, sa.m_id.m_addr_family,
                sa.m_id.m_dst_ip);
    }
    if(row->protocol != nullptr)
    {
        column.assign(row->protocol);
        if(column.compare(OVSREC_IPSEC_MANUAL_SA_PROTOCOL_AH)==0)
        {
            sa.m_id.m_protocol = static_cast<int>(ipsec_auth_method::ah);
        }
        else
        {
            sa.m_id.m_protocol = static_cast<int>(ipsec_auth_method::esp);
        }
    }
    if(row->selector_dest_prefix != nullptr)
    {
        column.assign(row->selector_dest_prefix);
        if(column.find_first_of(":")==std::string::npos)
        {
            /*IPv4*/
            sa.m_selector.m_addr_family = AF_INET;
        }
        else
        {
            /*IPv6*/
            sa.m_selector.m_addr_family = AF_INET6;
        }
        ipsecd_helper::set_dst_selector(column, sa.m_selector);
    }
    if(row->selector_src_prefix)
    {
        column.assign(row->selector_src_prefix);
        ipsecd_helper::set_src_selector(column, sa.m_selector);
    }
    if(row->authentication != nullptr)
    {
        column.assign(row->authentication);
        sa.m_auth_set = true;
        /*to lowercase*/
        std::transform(
                column.begin(), column.end(), column.begin(), ::tolower);
        /*Erase "HMAC"*/
        column.erase(column.end()-4, column.end());
        sa.m_auth.m_name.assign(column);
        sa.m_auth.m_key.assign(row->auth_key);
    }

    if(row->encryption != nullptr)
    {
        column.assign(row->encryption);
        std::transform(
                column.begin(), column.end(), column.begin(), ::tolower);
        sa.m_crypt.m_name.assign(column);
        sa.m_crypt.m_key.assign(row->encr_key);
    }

    /*stats, m_lifetime_current*/
    if(smap_get(&row->statistics,"add_time") != nullptr)
    {
        sa.m_lifetime_current.m_add_time = std::stoi(
                smap_get(&row->statistics,"add_time"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"use_time") != nullptr)
    {
        sa.m_lifetime_current.m_use_time = std::stoi(
                smap_get(&row->statistics, "use_time"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"bytes") != nullptr)
    {
        sa.m_lifetime_current.m_bytes = std::stoi(
                smap_get(&row->statistics, "bytes"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"packets") != nullptr)
    {
        sa.m_lifetime_current.m_packets = std::stoi(
                smap_get(&row->statistics, "packets"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"replay_window") != nullptr)
    {
        sa.m_stats.m_replay_window = std::stoi(
                smap_get(&row->statistics,"replay_window"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"replay") != nullptr)
    {
        sa.m_stats.m_replay = std::stoi(
                smap_get(&row->statistics,"replay"), nullptr, 0);
    }

    if(smap_get(&row->statistics,"integrity_failed") != nullptr)
    {
        sa.m_stats.m_integrity_failed = std::stoi(
                smap_get(&row->statistics,"integrity_failed"), nullptr, 0);
    }
}

void IPsecOvsdb::ipsec_sa_to_ovsrec(ipsec_sa& sa, ipsec_manual_sa_t& row)
{
}

void IPsecOvsdb::ipsec_sp_to_ovsrec(ipsec_sp& sp, ipsec_manual_sp_t& row)
{
}

void IPsecOvsdb::ovsrec_to_ipsec_sp(const ipsec_manual_sp_t row, ipsec_sp& sp)
{
    std::string value="";
    std::string ip_buffer = "";
    /*template object for m_template_list member*/
    ipsec_tmpl tmpl;

    if(row->direction != nullptr)
    {
        value.assign(row->direction);
        ipsecd_helper::str_to_ipsec_direction(value, sp.m_id.m_dir);
    }
    if(row->ip_family != nullptr)
    {
        value.assign(row->ip_family);
        if(value.compare(OVSREC_IPSEC_MANUAL_SP_IP_FAMILY_IPV4)==0)
        {

            sp.m_id.m_selector.m_addr_family = AF_INET;
        }
        else
        {
            sp.m_id.m_selector.m_addr_family = AF_INET6;
        }
    }
    if(row->dest_prefix != nullptr)
    {
        ip_buffer.assign(row->dest_prefix);
        ipsecd_helper::set_dst_selector(ip_buffer, sp.m_id.m_selector);
    }
    if(row->src_prefix != nullptr)
    {
        ip_buffer.assign(row->src_prefix);
        ipsecd_helper::set_src_selector(ip_buffer, sp.m_id.m_selector);
    }
    sp.m_priority = (uint32_t)(*row->priority);

    if(row->action != nullptr)
    {
        value.assign(row->action);
        ipsecd_helper::str_to_ipsec_action(value, sp.m_action);
    }
    tmpl.m_req_id = (uint32_t)(row->tmpl_request_id);
    if(row->tmpl_ip_family != nullptr)
    {
        value.assign(row->tmpl_ip_family);
        if(value.compare(OVSREC_IPSEC_MANUAL_SP_TMPL_IP_FAMILY_IPV4) == 0)
        {
            tmpl.m_addr_family = AF_INET;
        }
        else
        {
            tmpl.m_addr_family = AF_INET6;
        }
    }
    if(row->tmpl_mode)
    {
        value.assign(row->tmpl_mode);
        if(value.compare(OVSREC_IPSEC_MANUAL_SP_TMPL_MODE_TRANSPORT) == 0)
        {
            tmpl.m_mode = ipsec_mode::transport;
        }
        else
        {
            tmpl.m_mode =  ipsec_mode::tunnel;
        }
    }
    if(row->tmpl_protocol != nullptr)
    {
        value.assign(row->tmpl_protocol);
        if(value.compare(OVSREC_IPSEC_MANUAL_SP_TMPL_PROTOCOL_AH))
        {
            tmpl.m_protocol = static_cast<int>(ipsec_auth_method::ah);
        }
        else
        {
            tmpl.m_protocol = static_cast<int>(ipsec_auth_method::esp);
        }
    }
    if(row->tmpl_dest_ip != nullptr)
    {
        ip_buffer.assign(row->tmpl_dest_ip);
        ipsecd_helper::set_str_to_ip_addr_t(ip_buffer, tmpl.m_addr_family,
                tmpl.m_dst_ip);
    }
    if(row->tmpl_src_ip)
    {
        ip_buffer.assign(row->tmpl_src_ip);
        ipsecd_helper::set_str_to_ip_addr_t(ip_buffer, tmpl.m_addr_family,
                tmpl.m_src_ip);
    }
    sp.m_template_lists.push_back(tmpl);

    /*Geting stats*/
    if(smap_get(&row->statistics,"add_time") != nullptr)
    {
        sp.m_life_stats.m_add_time = std::stoi(
                smap_get(&row->statistics,"add_time"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"use_time") != nullptr)
    {
        sp.m_life_stats.m_use_time = std::stoi(
               smap_get(&row->statistics, "use_time"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"bytes") != nullptr)
    {
        sp.m_life_stats.m_bytes = std::stoi(
                smap_get(&row->statistics, "bytes"), nullptr, 0);
    }
    if(smap_get(&row->statistics,"packets") != nullptr)
    {
        sp.m_life_stats.m_packets = std::stoi(
                smap_get(&row->statistics, "packets"), nullptr, 0);
    }
}

void IPsecOvsdb::set_integer_to_column(const idl_row_t row,
                idl_column_t column, int64_t value)
{
    struct ovsdb_datum datum;
    union ovsdb_atom key;
    /*TODO: log info*/
    /*ovs_assert(inited);*/

    datum.n = 1;
    datum.keys = &key;
    key.integer = value;
    datum.values =  nullptr;
    m_idl_wrapper.idl_txn_write_clone(row, column, &datum);
}


ipsec_ret IPsecOvsdb::set_string_to_column(const idl_row_t row,
        idl_column_t column, const std::string& str_value)
{
    struct ovsdb_datum datum;
    union ovsdb_atom key;
    /*TODO: log info*/
    /*ovs_assert(inited);*/

    if(str_value.compare("") == 0)
    {
        return ipsec_ret::NULL_PARAMETERS;
    }
    else
    {
        datum.n = 1;
        datum.keys = &key;
        key.string =  CONST_CAST(char *, str_value.c_str());
    }
    datum.values = nullptr;
    m_idl_wrapper.idl_txn_write_clone(row, column, &datum);
    return ipsec_ret::OK;
}
