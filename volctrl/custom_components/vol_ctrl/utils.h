#pragma once

#include <string>

namespace esphome
{
    namespace vol_ctrl
    {
        namespace utils
        {

            // JSON parsing utilities
            bool extract_json_value(const std::string &json, const std::string &key, std::string &value);
            bool check_json_boolean(const std::string &response, const std::string &key, bool &result);
            bool extract_json_number(const std::string &response, const std::string &key, float &result);

            // Date/time helper functions
            std::string get_datetime_string();

        } // namespace utils
    } // namespace vol_ctrl
} // namespace esphome
