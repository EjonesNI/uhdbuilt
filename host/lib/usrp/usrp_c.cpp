/*
 * Copyright 2015 Ettus Research LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* C-Interface for multi_usrp */

#include <uhd/utils/static.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include <uhd/error.h>
#include <uhd/usrp/usrp.h>

#include <boost/foreach.hpp>
#include <boost/thread/mutex.hpp>

#include <string.h>
#include <map>

/****************************************************************************
 * Helpers
 ***************************************************************************/
uhd::stream_args_t stream_args_c_to_cpp(const uhd_stream_args_t *stream_args_c)
{
    std::string otw_format(stream_args_c->otw_format);
    std::string cpu_format(stream_args_c->cpu_format);
    std::string args(stream_args_c->args);
    std::vector<size_t> channels(stream_args_c->channel_list, stream_args_c->channel_list + stream_args_c->n_channels);

    uhd::stream_args_t stream_args_cpp(cpu_format, otw_format);
    stream_args_cpp.args = args;
    stream_args_cpp.channels = channels;

    return stream_args_cpp;
}

uhd::stream_cmd_t stream_cmd_c_to_cpp(const uhd_stream_cmd_t *stream_cmd_c)
{
    uhd::stream_cmd_t stream_cmd_cpp(uhd::stream_cmd_t::stream_mode_t(stream_cmd_c->stream_mode));
    stream_cmd_cpp.num_samps   = stream_cmd_c->num_samps;
    stream_cmd_cpp.stream_now  = stream_cmd_c->stream_now;
    stream_cmd_cpp.time_spec   = uhd::time_spec_t(stream_cmd_c->time_spec_full_secs, stream_cmd_c->time_spec_frac_secs);
    return stream_cmd_cpp;
}

/****************************************************************************
 * Registry / Pointer Management
 ***************************************************************************/
/* Public structs */
struct uhd_usrp {
    size_t usrp_index;
    std::string last_error;
};

struct uhd_tx_streamer {
    size_t usrp_index;
    size_t streamer_index;
    std::string last_error;
};

struct uhd_rx_streamer {
    size_t usrp_index;
    size_t streamer_index;
    std::string last_error;
};

/* Not public: We use this for our internal registry */
struct usrp_ptr {
    uhd::usrp::multi_usrp::sptr ptr;
    std::vector< uhd::rx_streamer::sptr > rx_streamers;
    std::vector< uhd::tx_streamer::sptr > tx_streamers;
    static size_t usrp_counter;
};
size_t usrp_ptr::usrp_counter = 0;
typedef struct usrp_ptr usrp_ptr;
/* Prefer map, because the list can be discontiguous */
typedef std::map<size_t, usrp_ptr> usrp_ptrs;

UHD_SINGLETON_FCN(usrp_ptrs, get_usrp_ptrs);
/* Shortcut for accessing the underlying USRP sptr from a uhd_usrp_handle* */
#define USRP(h_ptr) (get_usrp_ptrs()[h_ptr->usrp_index].ptr)
#define RX_STREAMER(h_ptr) (get_usrp_ptrs()[h_ptr->usrp_index].rx_streamers[h_ptr->streamer_index])
#define TX_STREAMER(h_ptr) (get_usrp_ptrs()[h_ptr->usrp_index].tx_streamers[h_ptr->streamer_index])

/****************************************************************************
 * RX Streamer
 ***************************************************************************/
static boost::mutex _rx_streamer_make_mutex;
uhd_error uhd_rx_streamer_make(uhd_rx_streamer_handle* h){
    UHD_SAFE_C(
        boost::mutex::scoped_lock(_rx_streamer_make_mutex);
        (*h) = new uhd_rx_streamer;
    )
}

static boost::mutex _rx_streamer_free_mutex;
uhd_error uhd_rx_streamer_free(uhd_rx_streamer_handle* h){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_rx_streamer_free_mutex);
        delete (*h);
        (*h) = NULL;
    )
}

uhd_error uhd_rx_streamer_num_channels(uhd_rx_streamer_handle h,
                                       size_t *num_channels_out){
    UHD_SAFE_C_SAVE_ERROR(h,
        *num_channels_out = RX_STREAMER(h)->get_num_channels();
    )
}

uhd_error uhd_rx_streamer_max_num_samps(uhd_rx_streamer_handle h,
                                        size_t *max_num_samps_out){
    UHD_SAFE_C_SAVE_ERROR(h,
        *max_num_samps_out = RX_STREAMER(h)->get_max_num_samps();
    )
}

uhd_error uhd_rx_streamer_recv(
    uhd_rx_streamer_handle h,
    void **buffs,
    size_t samps_per_buff,
    uhd_rx_metadata_handle md,
    double timeout,
    bool one_packet,
    size_t *items_recvd
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::rx_streamer::buffs_type buffs_cpp(buffs, RX_STREAMER(h)->get_num_channels());
        *items_recvd = RX_STREAMER(h)->recv(buffs_cpp, samps_per_buff, md->rx_metadata_cpp, timeout, one_packet);
    )
}

uhd_error uhd_rx_streamer_issue_stream_cmd(
    uhd_rx_streamer_handle h,
    const uhd_stream_cmd_t *stream_cmd
){
    UHD_SAFE_C_SAVE_ERROR(h,
        RX_STREAMER(h)->issue_stream_cmd(stream_cmd_c_to_cpp(stream_cmd));
    )
}

uhd_error uhd_rx_streamer_last_error(
    uhd_rx_streamer_handle h,
    char* error_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        memset(error_out, '\0', strbuffer_len);
        strncpy(error_out, h->last_error.c_str(), strbuffer_len);
    )
}

/****************************************************************************
 * TX Streamer
 ***************************************************************************/
static boost::mutex _tx_streamer_make_mutex;
uhd_error uhd_tx_streamer_make(
    uhd_tx_streamer_handle* h
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_tx_streamer_make_mutex);
        (*h) = new uhd_tx_streamer;
    )
}

static boost::mutex _tx_streamer_free_mutex;
uhd_error uhd_tx_streamer_free(
    uhd_tx_streamer_handle* h
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_tx_streamer_free_mutex);
        delete *h;
        *h = NULL;
    )
}

uhd_error uhd_tx_streamer_num_channels(
    uhd_tx_streamer_handle h,
    size_t *num_channels_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *num_channels_out = TX_STREAMER(h)->get_num_channels();
    )
}

uhd_error uhd_tx_streamer_max_num_samps(
    uhd_tx_streamer_handle h,
    size_t *max_num_samps_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *max_num_samps_out = TX_STREAMER(h)->get_max_num_samps();
    )
}

uhd_error uhd_tx_streamer_send(
    uhd_tx_streamer_handle h,
    const void **buffs,
    const size_t samps_per_buff,
    const uhd_tx_metadata_handle md,
    const double timeout,
    size_t *items_sent
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::tx_streamer::buffs_type buffs_cpp(buffs, TX_STREAMER(h)->get_num_channels());
        *items_sent = TX_STREAMER(h)->send(
            buffs_cpp,
            samps_per_buff,
            md->tx_metadata_cpp,
            timeout
        );
    )
}

uhd_error uhd_tx_streamer_recv_async_msg(
    uhd_tx_streamer_handle h,
    uhd_async_metadata_handle md,
    const double timeout,
    bool *valid
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *valid = TX_STREAMER(h)->recv_async_msg(md->async_metadata_cpp, timeout);
    )
}

uhd_error uhd_tx_streamer_last_error(
    uhd_tx_streamer_handle h,
    char* error_out,
    size_t strbuffer_len
){
    UHD_SAFE_C(
        memset(error_out, '\0', strbuffer_len);
        strncpy(error_out, h->last_error.c_str(), strbuffer_len);
    )
}

/****************************************************************************
 * Generate / Destroy API calls
 ***************************************************************************/
static boost::mutex _usrp_find_mutex;
uhd_error uhd_usrp_find(
    uhd_device_addrs_handle h,
    const char* args,
    size_t *num_found
){
    UHD_SAFE_C_SAVE_ERROR(h,
        boost::mutex::scoped_lock _lock(_usrp_find_mutex);

        h->device_addrs_cpp = uhd::device::find(std::string(args), uhd::device::USRP);
        *num_found = h->device_addrs_cpp.size();
    )
}

static boost::mutex _usrp_make_mutex;
uhd_error uhd_usrp_make(
    uhd_usrp_handle *h,
    const char *args
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_usrp_make_mutex);

        size_t usrp_count = usrp_ptr::usrp_counter;
        usrp_ptr::usrp_counter++;

        // Initialize USRP
        uhd::device_addr_t device_addr(args);
        usrp_ptr P;
        P.ptr = uhd::usrp::multi_usrp::make(device_addr);

        // Dump into registry
        get_usrp_ptrs()[usrp_count] = P;

        // Update handle
        (*h) = new uhd_usrp;
        (*h)->usrp_index = usrp_count;
    )
}

static boost::mutex _usrp_free_mutex;
uhd_error uhd_usrp_free(
    uhd_usrp_handle *h
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_usrp_free_mutex);

        if(!get_usrp_ptrs().count((*h)->usrp_index)){
            return UHD_ERROR_INVALID_DEVICE;
        }

        get_usrp_ptrs().erase((*h)->usrp_index);
        delete *h;
        *h = NULL;
    )
}

uhd_error uhd_usrp_last_error(
    uhd_usrp_handle h,
    char* error_out,
    size_t strbuffer_len
){
    UHD_SAFE_C(
        memset(error_out, '\0', strbuffer_len);
        strncpy(error_out, h->last_error.c_str(), strbuffer_len);
    )
}

static boost::mutex _usrp_get_rx_stream_mutex;
uhd_error uhd_usrp_get_rx_stream(
    uhd_usrp_handle h_u,
    uhd_stream_args_t *stream_args,
    uhd_rx_streamer_handle h_s
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_usrp_get_rx_stream_mutex);

        if(!get_usrp_ptrs().count(h_u->usrp_index)){
            return UHD_ERROR_INVALID_DEVICE;
        }

        usrp_ptr &usrp = get_usrp_ptrs()[h_u->usrp_index];
        usrp.rx_streamers.push_back(
            usrp.ptr->get_rx_stream(stream_args_c_to_cpp(stream_args))
        );
        h_s->usrp_index     = h_u->usrp_index;
        h_s->streamer_index = usrp.rx_streamers.size() - 1;
    )
}

static boost::mutex _usrp_get_tx_stream_mutex;
uhd_error uhd_usrp_get_tx_stream(
    uhd_usrp_handle h_u,
    uhd_stream_args_t *stream_args,
    uhd_tx_streamer_handle h_s
){
    UHD_SAFE_C(
        boost::mutex::scoped_lock lock(_usrp_get_tx_stream_mutex);

        if(!get_usrp_ptrs().count(h_u->usrp_index)){
            return UHD_ERROR_INVALID_DEVICE;
        }

        usrp_ptr &usrp = get_usrp_ptrs()[h_u->usrp_index];
        usrp.tx_streamers.push_back(
            usrp.ptr->get_tx_stream(stream_args_c_to_cpp(stream_args))
        );
        h_s->usrp_index     = h_u->usrp_index;
        h_s->streamer_index = usrp.tx_streamers.size() - 1;
    )
}

/****************************************************************************
 * multi_usrp API calls
 ***************************************************************************/

#define COPY_INFO_FIELD(out, dict, field) \
    out->field = strdup(dict.get(BOOST_STRINGIZE(field)).c_str())

uhd_error uhd_usrp_get_rx_info(
    uhd_usrp_handle h,
    size_t chan,
    uhd_usrp_rx_info_t *info_out
) {
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::dict<std::string, std::string> rx_info = USRP(h)->get_usrp_rx_info(chan);

        COPY_INFO_FIELD(info_out, rx_info, mboard_id);
        COPY_INFO_FIELD(info_out, rx_info, mboard_serial);
        COPY_INFO_FIELD(info_out, rx_info, rx_id);
        COPY_INFO_FIELD(info_out, rx_info, rx_subdev_name);
        COPY_INFO_FIELD(info_out, rx_info, rx_subdev_spec);
        COPY_INFO_FIELD(info_out, rx_info, rx_serial);
        COPY_INFO_FIELD(info_out, rx_info, rx_antenna);
    )
}

uhd_error uhd_usrp_get_tx_info(
    uhd_usrp_handle h,
    size_t chan,
    uhd_usrp_tx_info_t *info_out
) {
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::dict<std::string, std::string> tx_info = USRP(h)->get_usrp_tx_info(chan);

        COPY_INFO_FIELD(info_out, tx_info, mboard_id);
        COPY_INFO_FIELD(info_out, tx_info, mboard_serial);
        COPY_INFO_FIELD(info_out, tx_info, tx_id);
        COPY_INFO_FIELD(info_out, tx_info, tx_subdev_name);
        COPY_INFO_FIELD(info_out, tx_info, tx_subdev_spec);
        COPY_INFO_FIELD(info_out, tx_info, tx_serial);
        COPY_INFO_FIELD(info_out, tx_info, tx_antenna);
    )
}

/****************************************************************************
 * Motherboard methods
 ***************************************************************************/
uhd_error uhd_usrp_set_master_clock_rate(
    uhd_usrp_handle h,
    double rate,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_master_clock_rate(rate, mboard);
    )
}

uhd_error uhd_usrp_get_master_clock_rate(
    uhd_usrp_handle h,
    size_t mboard,
    double *clock_rate_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *clock_rate_out = USRP(h)->get_master_clock_rate(mboard);
    )
}

uhd_error uhd_usrp_get_pp_string(
    uhd_usrp_handle h,
    char* pp_string_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        strncpy(pp_string_out, USRP(h)->get_pp_string().c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_mboard_name(
    uhd_usrp_handle h,
    size_t mboard,
    char* mboard_name_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        strncpy(mboard_name_out, USRP(h)->get_mboard_name(mboard).c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_time_now(
    uhd_usrp_handle h,
    size_t mboard,
    time_t *full_secs_out,
    double *frac_secs_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp = USRP(h)->get_time_now(mboard);
        *full_secs_out = time_spec_cpp.get_full_secs();
        *frac_secs_out = time_spec_cpp.get_frac_secs();
    )
}

uhd_error uhd_usrp_get_time_last_pps(
    uhd_usrp_handle h,
    size_t mboard,
    time_t *full_secs_out,
    double *frac_secs_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp = USRP(h)->get_time_last_pps(mboard);
        *full_secs_out = time_spec_cpp.get_full_secs();
        *frac_secs_out = time_spec_cpp.get_frac_secs();
    )
}

uhd_error uhd_usrp_set_time_now(
    uhd_usrp_handle h,
    time_t full_secs,
    double frac_secs,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp(full_secs, frac_secs);
        USRP(h)->set_time_now(time_spec_cpp, mboard);
    )
}

uhd_error uhd_usrp_set_time_next_pps(
    uhd_usrp_handle h,
    time_t full_secs,
    double frac_secs,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp(full_secs, frac_secs);
        USRP(h)->set_time_next_pps(time_spec_cpp, mboard);
    )
}

uhd_error uhd_usrp_set_time_unknown_pps(
    uhd_usrp_handle h,
    time_t full_secs,
    double frac_secs
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp(full_secs, frac_secs);
        USRP(h)->set_time_unknown_pps(time_spec_cpp);
    )
}

uhd_error uhd_usrp_get_time_synchronized(
    uhd_usrp_handle h,
    bool *result_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *result_out = USRP(h)->get_time_synchronized();
        return UHD_ERROR_NONE;
    )
}

uhd_error uhd_usrp_set_command_time(
    uhd_usrp_handle h,
    time_t full_secs,
    double frac_secs,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::time_spec_t time_spec_cpp(full_secs, frac_secs);
        USRP(h)->set_command_time(time_spec_cpp, mboard);
    )
}

uhd_error uhd_usrp_clear_command_time(
    uhd_usrp_handle h,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->clear_command_time(mboard);
    )
}

uhd_error uhd_usrp_issue_stream_cmd(
    uhd_usrp_handle h,
    uhd_stream_cmd_t *stream_cmd,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->issue_stream_cmd(stream_cmd_c_to_cpp(stream_cmd), chan);
    )
}

uhd_error uhd_usrp_set_time_source(
    uhd_usrp_handle h,
    const char* time_source,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_time_source(std::string(time_source), mboard);
    )
}

uhd_error uhd_usrp_get_time_source(
    uhd_usrp_handle h,
    size_t mboard,
    char* time_source_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        strncpy(time_source_out, USRP(h)->get_time_source(mboard).c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_time_sources(
    uhd_usrp_handle h,
    size_t mboard,
    char* time_sources_out,
    size_t strbuffer_len,
    size_t *num_time_sources_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> time_sources = USRP(h)->get_time_sources(mboard);
        *num_time_sources_out = time_sources.size();

        std::string time_sources_str = "";
        BOOST_FOREACH(const std::string &time_source, time_sources){
            time_sources_str += time_source;
            time_sources_str += ',';
        }
        if(time_sources.size() > 0){
            time_sources_str.resize(time_sources_str.size()-1);
        }

        memset(time_sources_out, '\0', strbuffer_len);
        strncpy(time_sources_out, time_sources_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_clock_source(
    uhd_usrp_handle h,
    const char* clock_source,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_clock_source(std::string(clock_source), mboard);
    )
}

uhd_error uhd_usrp_get_clock_source(
    uhd_usrp_handle h,
    size_t mboard,
    char* clock_source_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        strncpy(clock_source_out, USRP(h)->get_clock_source(mboard).c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_clock_sources(
    uhd_usrp_handle h,
    size_t mboard,
    char* clock_sources_out,
    size_t strbuffer_len,
    size_t *num_clock_sources_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> clock_sources = USRP(h)->get_clock_sources(mboard);
        *num_clock_sources_out = clock_sources.size();

        std::string clock_sources_str = "";
        BOOST_FOREACH(const std::string &clock_source, clock_sources){
            clock_sources_str += clock_source;
            clock_sources_str += ',';
        }
        if(clock_sources.size() > 0){
            clock_sources_str.resize(clock_sources_str.size()-1);
        }

        memset(clock_sources_out, '\0', strbuffer_len);
        strncpy(clock_sources_out, clock_sources_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_clock_source_out(
    uhd_usrp_handle h,
    bool enb,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_clock_source_out(enb, mboard);
    )
}

uhd_error uhd_usrp_get_num_mboards(
    uhd_usrp_handle h,
    size_t *num_mboards_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *num_mboards_out = USRP(h)->get_num_mboards();
    )
}

uhd_error uhd_usrp_get_mboard_sensor(
    uhd_usrp_handle h,
    const char* name,
    size_t mboard,
    uhd_sensor_value_handle sensor_value_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        delete sensor_value_out->sensor_value_cpp;
        sensor_value_out->sensor_value_cpp = new uhd::sensor_value_t(USRP(h)->get_mboard_sensor(name, mboard));
    )
}

uhd_error uhd_usrp_get_mboard_sensor_names(
    uhd_usrp_handle h,
    size_t mboard,
    char* mboard_sensor_names_out,
    size_t strbuffer_len,
    size_t *num_mboard_sensors_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> mboard_sensor_names = USRP(h)->get_mboard_sensor_names(mboard);
        *num_mboard_sensors_out = mboard_sensor_names.size();

        std::string mboard_sensor_names_str = "";
        BOOST_FOREACH(const std::string &mboard_sensor_name, mboard_sensor_names){
            mboard_sensor_names_str += mboard_sensor_name;
            mboard_sensor_names_str += ',';
        }
        if(mboard_sensor_names.size() > 0){
            mboard_sensor_names_str.resize(mboard_sensor_names_str.size()-1);
        }

        memset(mboard_sensor_names_out, '\0', strbuffer_len);
        strncpy(mboard_sensor_names_out, mboard_sensor_names_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_user_register(
    uhd_usrp_handle h,
    uint8_t addr,
    uint32_t data,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_user_register(addr, data, mboard);
    )
}

/****************************************************************************
 * EEPROM access methods
 ***************************************************************************/

uhd_error uhd_usrp_get_mboard_eeprom(
    uhd_usrp_handle h,
    uhd_mboard_eeprom_handle mb_eeprom,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::fs_path eeprom_path = str(boost::format("/mboards/%d/eeprom")
                                       % mboard);

        uhd::property_tree::sptr ptree = USRP(h)->get_device()->get_tree();
        mb_eeprom->mboard_eeprom_cpp = ptree->access<uhd::usrp::mboard_eeprom_t>(eeprom_path).get();
    )
}

uhd_error uhd_usrp_set_mboard_eeprom(
    uhd_usrp_handle h,
    uhd_mboard_eeprom_handle mb_eeprom,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::fs_path eeprom_path = str(boost::format("/mboards/%d/eeprom")
                                       % mboard);

        uhd::property_tree::sptr ptree = USRP(h)->get_device()->get_tree();
        ptree->access<uhd::usrp::mboard_eeprom_t>(eeprom_path).set(mb_eeprom->mboard_eeprom_cpp);
    )
}

uhd_error uhd_usrp_get_dboard_eeprom(
    uhd_usrp_handle h,
    uhd_dboard_eeprom_handle db_eeprom,
    const char* unit,
    const char* slot,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::fs_path eeprom_path = str(boost::format("/mboards/%d/dboards/%s/%s_eeprom")
                                       % mboard % slot % unit);

        uhd::property_tree::sptr ptree = USRP(h)->get_device()->get_tree();
        db_eeprom->dboard_eeprom_cpp = ptree->access<uhd::usrp::dboard_eeprom_t>(eeprom_path).get();
    )
}

uhd_error uhd_usrp_set_dboard_eeprom(
    uhd_usrp_handle h,
    uhd_dboard_eeprom_handle db_eeprom,
    const char* unit,
    const char* slot,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::fs_path eeprom_path = str(boost::format("/mboards/%d/dboards/%s/%s_eeprom")
                                       % mboard % slot % unit);

        uhd::property_tree::sptr ptree = USRP(h)->get_device()->get_tree();
        ptree->access<uhd::usrp::dboard_eeprom_t>(eeprom_path).set(db_eeprom->dboard_eeprom_cpp);
    )
}

/****************************************************************************
 * RX methods
 ***************************************************************************/

uhd_error uhd_usrp_set_rx_subdev_spec(
    uhd_usrp_handle h,
    uhd_subdev_spec_handle subdev_spec,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_subdev_spec(subdev_spec->subdev_spec_cpp, mboard);
    )
}

uhd_error uhd_usrp_get_rx_subdev_spec(
    uhd_usrp_handle h,
    size_t mboard,
    uhd_subdev_spec_handle subdev_spec_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        subdev_spec_out->subdev_spec_cpp = USRP(h)->get_rx_subdev_spec(mboard);
    )
}

uhd_error uhd_usrp_get_rx_num_channels(
    uhd_usrp_handle h,
    size_t *num_channels_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *num_channels_out = USRP(h)->get_rx_num_channels();
    )
}

uhd_error uhd_usrp_get_rx_subdev_name(
    uhd_usrp_handle h,
    size_t chan,
    char* rx_subdev_name_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string rx_subdev_name = USRP(h)->get_rx_subdev_name(chan);
        strncpy(rx_subdev_name_out, rx_subdev_name.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_rx_rate(
    uhd_usrp_handle h,
    double rate,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_rate(rate, chan);
    )
}

uhd_error uhd_usrp_get_rx_rate(
    uhd_usrp_handle h,
    size_t chan,
    double *rate_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *rate_out = USRP(h)->get_rx_rate(chan);
    )
}

uhd_error uhd_usrp_get_rx_rates(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle rates_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        rates_out->meta_range_cpp = USRP(h)->get_rx_rates(chan);
    )
}

uhd_error uhd_usrp_set_rx_freq(
    uhd_usrp_handle h,
    uhd_tune_request_t *tune_request,
    size_t chan,
    uhd_tune_result_t *tune_result
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::tune_request_t tune_request_cpp = uhd_tune_request_c_to_cpp(tune_request);
        uhd::tune_result_t tune_result_cpp = USRP(h)->set_rx_freq(tune_request_cpp, chan);
        uhd_tune_result_cpp_to_c(tune_result_cpp, tune_result);
    )
}

uhd_error uhd_usrp_get_rx_freq(
    uhd_usrp_handle h,
    size_t chan,
    double *freq_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *freq_out = USRP(h)->get_rx_freq(chan);
    )
}

uhd_error uhd_usrp_get_rx_freq_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle freq_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        freq_range_out->meta_range_cpp = USRP(h)->get_rx_freq_range(chan);
    )
}

uhd_error uhd_usrp_get_fe_rx_freq_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle freq_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        freq_range_out->meta_range_cpp = USRP(h)->get_fe_rx_freq_range(chan);
    )
}

uhd_error uhd_usrp_set_rx_gain(
    uhd_usrp_handle h,
    double gain,
    size_t chan,
    const char *gain_name
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string name(gain_name);
        if(name.empty()){
            USRP(h)->set_rx_gain(gain, chan);
        }
        else{
            USRP(h)->set_rx_gain(gain, name, chan);
        }
    )
}

uhd_error uhd_usrp_set_normalized_rx_gain(
    uhd_usrp_handle h,
    double gain,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_normalized_rx_gain(gain, chan);
    )
}

uhd_error uhd_usrp_set_rx_agc(
    uhd_usrp_handle h,
    bool enable,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_agc(enable, chan);
    )
}

uhd_error uhd_usrp_get_rx_gain(
    uhd_usrp_handle h,
    size_t chan,
    const char *gain_name,
    double *gain_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string name(gain_name);
        if(name.empty()){
            *gain_out = USRP(h)->get_rx_gain(chan);
        }
        else{
            *gain_out = USRP(h)->get_rx_gain(name, chan);
        }
    )
}

uhd_error uhd_usrp_get_normalized_rx_gain(
    uhd_usrp_handle h,
    size_t chan,
    double *gain_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *gain_out = USRP(h)->get_normalized_rx_gain(chan);
    )
}

uhd_error uhd_usrp_get_rx_gain_range(
    uhd_usrp_handle h,
    const char* name,
    size_t chan,
    uhd_meta_range_handle gain_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        gain_range_out->meta_range_cpp = USRP(h)->get_rx_gain_range(name, chan);
    )
}

uhd_error uhd_usrp_get_rx_gain_names(
    uhd_usrp_handle h,
    size_t chan,
    char* gain_names_out,
    size_t strbuffer_len,
    size_t *num_rx_gain_names_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> rx_gain_names = USRP(h)->get_rx_gain_names(chan);
        *num_rx_gain_names_out = rx_gain_names.size();

        std::string rx_gain_names_str = "";
        BOOST_FOREACH(const std::string &gain_name, rx_gain_names){
            rx_gain_names_str += gain_name;
            rx_gain_names_str += ',';
        }
        if(rx_gain_names.size() > 0){
            rx_gain_names_str.resize(rx_gain_names_str.size()-1);
        }

        memset(gain_names_out, '\0', strbuffer_len);
        strncpy(gain_names_out, rx_gain_names_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_rx_antenna(
    uhd_usrp_handle h,
    const char* ant,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_antenna(std::string(ant), chan);
    )
}

uhd_error uhd_usrp_get_rx_antenna(
    uhd_usrp_handle h,
    size_t chan,
    char* ant_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string rx_antenna = USRP(h)->get_rx_antenna(chan);
        strncpy(ant_out, rx_antenna.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_rx_antennas(
    uhd_usrp_handle h,
    size_t chan,
    char* antennas_out,
    size_t strbuffer_len,
    size_t *num_rx_antennas_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> rx_antennas = USRP(h)->get_rx_antennas(chan);
        *num_rx_antennas_out = rx_antennas.size();

        std::string rx_antennas_str = "";
        BOOST_FOREACH(const std::string &rx_antenna, rx_antennas){
            rx_antennas_str += rx_antenna;
            rx_antennas_str += ',';
        }
        if(rx_antennas.size() > 0){
            rx_antennas_str.resize(rx_antennas_str.size()-1);
        }

        memset(antennas_out, '\0', strbuffer_len);
        strncpy(antennas_out, rx_antennas_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_rx_bandwidth(
    uhd_usrp_handle h,
    double bandwidth,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_bandwidth(bandwidth, chan);
    )
}

uhd_error uhd_usrp_get_rx_bandwidth(
    uhd_usrp_handle h,
    size_t chan,
    double *bandwidth_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *bandwidth_out = USRP(h)->get_rx_bandwidth(chan);
    )
}

uhd_error uhd_usrp_get_rx_bandwidth_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle bandwidth_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        bandwidth_range_out->meta_range_cpp = USRP(h)->get_rx_bandwidth_range(chan);
    )
}

uhd_error uhd_usrp_get_rx_sensor(
    uhd_usrp_handle h,
    const char* name,
    size_t chan,
    uhd_sensor_value_handle sensor_value_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        delete sensor_value_out->sensor_value_cpp;
        sensor_value_out->sensor_value_cpp = new uhd::sensor_value_t(USRP(h)->get_rx_sensor(name, chan));
    )
}

uhd_error uhd_usrp_get_rx_sensor_names(
    uhd_usrp_handle h,
    size_t chan,
    char* sensor_names_out,
    size_t strbuffer_len,
    size_t *num_rx_sensors_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> rx_sensor_names = USRP(h)->get_rx_sensor_names(chan);
        *num_rx_sensors_out = rx_sensor_names.size();

        std::string rx_sensor_names_str = "";
        BOOST_FOREACH(const std::string &rx_sensor_name, rx_sensor_names){
            rx_sensor_names_str += rx_sensor_name;
            rx_sensor_names_str += ',';
        }
        if(rx_sensor_names.size() > 0){
            rx_sensor_names_str.resize(rx_sensor_names_str.size()-1);
        }

        memset(sensor_names_out, '\0', strbuffer_len);
        strncpy(sensor_names_out, rx_sensor_names_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_rx_dc_offset_enabled(
    uhd_usrp_handle h,
    bool enb,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_dc_offset(enb, chan);
    )
}

uhd_error uhd_usrp_set_rx_iq_balance_enabled(
    uhd_usrp_handle h,
    bool enb,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_rx_iq_balance(enb, chan);
    )
}

/****************************************************************************
 * TX methods
 ***************************************************************************/

uhd_error uhd_usrp_set_tx_subdev_spec(
    uhd_usrp_handle h,
    uhd_subdev_spec_handle subdev_spec,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_subdev_spec(subdev_spec->subdev_spec_cpp, mboard);
    )
}

uhd_error uhd_usrp_get_tx_subdev_spec(
    uhd_usrp_handle h,
    size_t mboard,
    uhd_subdev_spec_handle subdev_spec_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        subdev_spec_out->subdev_spec_cpp = USRP(h)->get_tx_subdev_spec(mboard);
    )
}


uhd_error uhd_usrp_get_tx_num_channels(
    uhd_usrp_handle h,
    size_t *num_channels_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *num_channels_out = USRP(h)->get_tx_num_channels();
    )
}

uhd_error uhd_usrp_get_tx_subdev_name(
    uhd_usrp_handle h,
    size_t chan,
    char* tx_subdev_name_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string tx_subdev_name = USRP(h)->get_tx_subdev_name(chan);
        strncpy(tx_subdev_name_out, tx_subdev_name.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_tx_rate(
    uhd_usrp_handle h,
    double rate,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_rate(rate, chan);
    )
}

uhd_error uhd_usrp_get_tx_rate(
    uhd_usrp_handle h,
    size_t chan,
    double *rate_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *rate_out = USRP(h)->get_tx_rate(chan);
    )
}

uhd_error uhd_usrp_get_tx_rates(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle rates_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        rates_out->meta_range_cpp = USRP(h)->get_tx_rates(chan);
    )
}

uhd_error uhd_usrp_set_tx_freq(
    uhd_usrp_handle h,
    uhd_tune_request_t *tune_request,
    size_t chan,
    uhd_tune_result_t *tune_result
){
    UHD_SAFE_C_SAVE_ERROR(h,
        uhd::tune_request_t tune_request_cpp = uhd_tune_request_c_to_cpp(tune_request);
        uhd::tune_result_t tune_result_cpp = USRP(h)->set_tx_freq(tune_request_cpp, chan);
        uhd_tune_result_cpp_to_c(tune_result_cpp, tune_result);
    )
}

uhd_error uhd_usrp_get_tx_freq(
    uhd_usrp_handle h,
    size_t chan,
    double *freq_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *freq_out = USRP(h)->get_tx_freq(chan);
    )
}

uhd_error uhd_usrp_get_tx_freq_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle freq_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        freq_range_out->meta_range_cpp = USRP(h)->get_tx_freq_range(chan);
    )
}

uhd_error uhd_usrp_get_fe_tx_freq_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle freq_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        freq_range_out->meta_range_cpp = USRP(h)->get_fe_tx_freq_range(chan);
    )
}

uhd_error uhd_usrp_set_tx_gain(
    uhd_usrp_handle h,
    double gain,
    size_t chan,
    const char *gain_name
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string name(gain_name);
        if(name.empty()){
            USRP(h)->set_tx_gain(gain, chan);
        }
        else{
            USRP(h)->set_tx_gain(gain, name, chan);
        }
    )
}

uhd_error uhd_usrp_set_normalized_tx_gain(
    uhd_usrp_handle h,
    double gain,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_normalized_tx_gain(gain, chan);
    )
}

uhd_error uhd_usrp_get_tx_gain(
    uhd_usrp_handle h,
    size_t chan,
    const char *gain_name,
    double *gain_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string name(gain_name);
        if(name.empty()){
            *gain_out = USRP(h)->get_tx_gain(chan);
        }
        else{
            *gain_out = USRP(h)->get_tx_gain(name, chan);
        }
    )
}

uhd_error uhd_usrp_get_normalized_tx_gain(
    uhd_usrp_handle h,
    size_t chan,
    double *gain_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *gain_out = USRP(h)->get_normalized_tx_gain(chan);
    )
}

uhd_error uhd_usrp_get_tx_gain_range(
    uhd_usrp_handle h,
    const char* name,
    size_t chan,
    uhd_meta_range_handle gain_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        gain_range_out->meta_range_cpp = USRP(h)->get_tx_gain_range(name, chan);
    )
}

uhd_error uhd_usrp_get_tx_gain_names(
    uhd_usrp_handle h,
    size_t chan,
    char* gain_names_out,
    size_t strbuffer_len,
    size_t *num_tx_gain_names_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> tx_gain_names = USRP(h)->get_tx_gain_names(chan);
        *num_tx_gain_names_out = tx_gain_names.size();

        std::string tx_gain_names_str = "";
        BOOST_FOREACH(const std::string &tx_gain_name, tx_gain_names){
            tx_gain_names_str += tx_gain_name;
            tx_gain_names_str += ',';
        }
        if(tx_gain_names.size() > 0){
            tx_gain_names_str.resize(tx_gain_names_str.size()-1);
        }

        memset(gain_names_out, '\0', strbuffer_len);
        strncpy(gain_names_out, tx_gain_names_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_tx_antenna(
    uhd_usrp_handle h,
    const char* ant,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_antenna(std::string(ant), chan);
    )
}

uhd_error uhd_usrp_get_tx_antenna(
    uhd_usrp_handle h,
    size_t chan,
    char* ant_out,
    size_t strbuffer_len
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::string tx_antenna = USRP(h)->get_tx_antenna(chan);
        strncpy(ant_out, tx_antenna.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_get_tx_antennas(
    uhd_usrp_handle h,
    size_t chan,
    char* antennas_out,
    size_t strbuffer_len,
    size_t *num_tx_antennas_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> tx_antennas = USRP(h)->get_tx_antennas(chan);
        *num_tx_antennas_out = tx_antennas.size();

        std::string tx_antennas_str = "";
        BOOST_FOREACH(const std::string &tx_antenna, tx_antennas){
            tx_antennas_str += tx_antenna;
            tx_antennas_str += ',';
        }
        if(tx_antennas.size() > 0){
            tx_antennas_str.resize(tx_antennas_str.size()-1);
        }

        memset(antennas_out, '\0', strbuffer_len);
        strncpy(antennas_out, tx_antennas_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_tx_bandwidth(
    uhd_usrp_handle h,
    double bandwidth,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_bandwidth(bandwidth, chan);
    )
}

uhd_error uhd_usrp_get_tx_bandwidth(
    uhd_usrp_handle h,
    size_t chan,
    double *bandwidth_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *bandwidth_out = USRP(h)->get_tx_bandwidth(chan);
    )
}

uhd_error uhd_usrp_get_tx_bandwidth_range(
    uhd_usrp_handle h,
    size_t chan,
    uhd_meta_range_handle bandwidth_range_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        bandwidth_range_out->meta_range_cpp = USRP(h)->get_tx_bandwidth_range(chan);
    )
}

uhd_error uhd_usrp_get_tx_sensor(
    uhd_usrp_handle h,
    const char* name,
    size_t chan,
    uhd_sensor_value_handle sensor_value_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        delete sensor_value_out->sensor_value_cpp;
        sensor_value_out->sensor_value_cpp = new uhd::sensor_value_t(USRP(h)->get_tx_sensor(name, chan));
    )
}

uhd_error uhd_usrp_get_tx_sensor_names(
    uhd_usrp_handle h,
    size_t chan,
    char* sensor_names_out,
    size_t strbuffer_len,
    size_t *num_tx_sensors_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> tx_sensor_names = USRP(h)->get_tx_sensor_names(chan);
        *num_tx_sensors_out = tx_sensor_names.size();

        std::string tx_sensor_names_str = "";
        BOOST_FOREACH(const std::string &tx_sensor_name, tx_sensor_names){
            tx_sensor_names_str += tx_sensor_name;
            tx_sensor_names_str += ',';
        }
        if(tx_sensor_names.size() > 0){
            tx_sensor_names_str.resize(tx_sensor_names_str.size()-1);
        }

        memset(sensor_names_out, '\0', strbuffer_len);
        strncpy(sensor_names_out, tx_sensor_names_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_tx_dc_offset_enabled(
    uhd_usrp_handle h,
    bool enb,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_dc_offset(enb, chan);
    )
}

uhd_error uhd_usrp_set_tx_iq_balance_enabled(
    uhd_usrp_handle h,
    bool enb,
    size_t chan
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_tx_iq_balance(enb, chan);
    )
}

/****************************************************************************
 * GPIO methods
 ***************************************************************************/

uhd_error uhd_usrp_get_gpio_banks(
    uhd_usrp_handle h,
    size_t chan,
    char* gpio_banks_out,
    size_t strbuffer_len,
    size_t *num_gpio_banks_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        std::vector<std::string> gpio_banks = USRP(h)->get_gpio_banks(chan);
        *num_gpio_banks_out = gpio_banks.size();

        std::string gpio_banks_str = "";
        BOOST_FOREACH(const std::string &gpio_bank, gpio_banks){
            gpio_banks_str += gpio_bank;
            gpio_banks_str += ',';
        }
        if(gpio_banks.size() > 0){
            gpio_banks_str.resize(gpio_banks_str.size()-1);
        }

        memset(gpio_banks_out, '\0', strbuffer_len);
        strncpy(gpio_banks_out, gpio_banks_str.c_str(), strbuffer_len);
    )
}

uhd_error uhd_usrp_set_gpio_attr(
    uhd_usrp_handle h,
    const char* bank,
    const char* attr,
    uint32_t value,
    uint32_t mask,
    size_t mboard
){
    UHD_SAFE_C_SAVE_ERROR(h,
        USRP(h)->set_gpio_attr(std::string(bank), std::string(attr),
                               value, mask, mboard);
    )
}

uhd_error uhd_usrp_get_gpio_attr(
    uhd_usrp_handle h,
    const char* bank,
    const char* attr,
    size_t mboard,
    uint32_t *attr_out
){
    UHD_SAFE_C_SAVE_ERROR(h,
        *attr_out = USRP(h)->get_gpio_attr(std::string(bank), std::string(attr), mboard);
    )
}