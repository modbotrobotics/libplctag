#include <stdio.h>
#include <signal.h>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include "../lib/libplctag.h"
#include "utils.h"

const unsigned int g_data_timeout = 5000;
const unsigned int g_debug = 4;
const std::string g_cpu = "LGX";
const std::string g_gateway = "192.168.1.200";
const std::string g_path = "1,0";  // backplane, slot
const std::string g_protocol = "ab-eip";
const std::string g_tag_path = "protocol=ab_eip&gateway=10.206.1.27&path=1,0&cpu=LGX&elem_size=88&elem_count=48&debug=1&name=Loc_Txt";

// Strings are 80 bytes, prefaced with 2 bytes of size information (Magna)
const unsigned int STRING_DATA_SIZE = 80;
const unsigned int STRING_SIZE_PADDING_SIZE = 2;
const unsigned int STRING_SIZE = 82;

std::function<void(void)> l_sig_handler;
bool g_run = true;

void signal_handler(int sig) {
  g_run = false;
}

/*
 * Notes:
 * - Tags are treated like arrays. A tag that contains a single data element is treated like an array of size one.
 * - In order to create a tag, you must know what protocol you are going to use and any arguments that that protocol requires.
 *    The entire set of information for accessing a tag is contained in a string that is passed to the plc_tag_create
 *    function. This string is formatted in a manner similar to a URL.
 *    It is composed of key-value pairs delimited by ampersands.
 * - plc_tag_create Returns an integer handle to a tag in most cases. If there was an error that prevented any creation
 *    of the tag at all (i.e. no memory), a negative value will be returned.
 * - Note that the actual data size of a string is 88 bytes, not 82+4.
 * - STRING types are a DINT (4 bytes) followed by 82 bytes of characters.  Then two bytes of padding.
 */



/**
 * @param e_size element size in bytes
 * @param e_count number of elements in array
 * @return tag path string
 */
const char* create_tag_path(std::string name, unsigned int e_size, unsigned int e_count = 1) {
  std::stringstream ss;
  ss << "protocol=" << g_protocol 
    << "&gateway=" << g_gateway 
    << "&path=" << g_path 
    << "&cpu=" << g_cpu
    << "&elem_size=" << std::to_string(e_size) 
    << "&elem_count=" << std::to_string(e_count)
    << "&debug=" << g_debug
    << "&name=" << name;
  fprintf(stdout, "- created tag path \"%s\"\n", ss.str().c_str());
  // fprintf(stdout, "-                 (\"%s\")\n", g_tag_path.c_str());
  // return g_tag_path.c_str();
  return ss.str().c_str();
}

std::string plc_tag_get_string(int32_t tag) {
  int str_size = plc_tag_get_int16(tag, STRING_SIZE);
  char str[STRING_SIZE] = {0};
  int j;

  for(j = 0; j < str_size; j++) {
    str[j] = (char)plc_tag_get_uint8(tag, j + STRING_SIZE_PADDING_SIZE);
  }
  str[j] = (char)0;

  printf("read string (%d chars) '%s'\n", str_size, str);
  return str;
}

void plc_tag_set_string(int32_t tag, std::string str) {
  int base_offset = 0;  // Only used in string arrays (would be i * STRING_SIZE);

  /* now write the data */
  int16_t str_index = 0;
  int16_t str_len = static_cast<int16_t>(str.length());

  /* set the length - 2 bytes */
  plc_tag_set_int16(tag, base_offset, str_len);

  /* copy the data */
  while ((str_index < str_len) && (str_index < STRING_DATA_SIZE)) {
    plc_tag_set_uint8(tag, base_offset + STRING_SIZE_PADDING_SIZE + str_index, (uint8_t)str[str_index]);
    str_index++;
  }

  /* pad with zeros */
  while(str_index < STRING_DATA_SIZE) {
    plc_tag_set_uint8(tag, base_offset + STRING_SIZE_PADDING_SIZE + str_index, 0);
    str_index++;
  }
}


int main() {
  signal(SIGABRT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGPIPE, SIG_IGN);

  int rc;
  unsigned int loop_i = 0;
  uint16_t mode = 0;
  uint16_t state = 0;

  // Create tags
  fprintf(stdout, "Creating tags...\n");
  std::map<std::string, int32_t> tags;
  for (auto& key : {"FaultMessage", "Mode", "SequenceName", "SequenceRun", "State", "StateName"}) {
    tags[key] = 0;
  }

  tags["FaultMessage"] = plc_tag_create(create_tag_path("FaultMessage", STRING_SIZE, 1), g_data_timeout);
  tags["Mode"] = plc_tag_create(create_tag_path("Mode", 2, 1), g_data_timeout);
  tags["SequenceName"] = plc_tag_create(create_tag_path("SequenceName", STRING_SIZE, 1), g_data_timeout);
  tags["SequenceRun"] = plc_tag_create(create_tag_path("SequenceRun", 1, 1), g_data_timeout);
  tags["State"] = plc_tag_create(create_tag_path("State", 2, 1), g_data_timeout);
  for (auto& kv : tags) {
    if (kv.second < 0) {
      fprintf(stdout, "ERROR: Could not create tag \"%s\" - %s\n", kv.first.c_str(), plc_tag_decode_error(kv.second));
      return 0;
    }
  }

  // Tag status
  for (auto& kv : tags) {
    if ((rc = plc_tag_status(kv.second)) != PLCTAG_STATUS_OK) {
      fprintf(stdout, "ERROR: Failed when setting up tag \"%s\" internal state. Error %s\n", kv.first.c_str(), plc_tag_decode_error(rc));
      plc_tag_destroy(kv.second);
      return 0;
    }
  }

  while (g_run) {
    // Read tags
    fprintf(stdout, "Reading tags...");
    for (auto& kv : tags) {
      rc = plc_tag_read(kv.second, g_data_timeout);
      if (rc != PLCTAG_STATUS_OK) {
        fprintf(stdout, "ERROR: Unable to read the data from tag \"%s\"! Got error code %d: %s\n", kv.first.c_str(), rc, plc_tag_decode_error(rc));
        plc_tag_destroy(kv.second);
        return 0;
      }
    }

    fprintf(stdout, "- Read tag \"fault_message\" data: %s\n", plc_tag_get_string(tags["FaultMessage"]).c_str());
    fprintf(stdout, "- Read tag \"mode\" data: %u\n", plc_tag_get_uint16(tags["Mode"], 0));
    fprintf(stdout, "- Read tag \"sequence_name\" data: %s\n", plc_tag_get_string(tags["SequenceName"]).c_str());
    fprintf(stdout, "- Read tag \"sequence_run\" data: %u\n", plc_tag_get_uint8(tags["SequenceRun"], 0));
    fprintf(stdout, "- Read tag \"state\" data: %u\n", plc_tag_get_uint16(tags["State"], 0));
    fprintf(stdout, "- Read tag \"state_name\" data: %s\n", plc_tag_get_string(tags["StateName"]).c_str());


    // Write tags
    fprintf(stdout, "Writing tags...\n");
  
    std::stringstream ss_fault_message;
    std::stringstream ss_state_name;
    ss_fault_message << "this is a fault message (" << loop_i << ")";
    ss_state_name << "State" << loop_i;
  
    mode = (mode + 1) % 3;
    state++;
    fprintf(stdout, "- Setting tag %s to %s\n", "FaultMessage", ss_fault_message.str().c_str());
    fprintf(stdout, "- Setting tag %s to %u\n", "Mode", mode);
    fprintf(stdout, "- Setting tag %s to %u\n", "State", state);
    fprintf(stdout, "- Setting tag %s to %s\n", "StateName", ss_state_name.str().c_str());
    plc_tag_set_string(tags["FaultMessage"], ss_fault_message.str());
    plc_tag_set_uint16(tags["Mode"], 0, mode);
    plc_tag_set_uint16(tags["State"], 0, state);
    plc_tag_set_string(tags["StateName"], ss_state_name.str());

    rc = plc_tag_write(tags["FaultMessage"], g_data_timeout);
    if (rc != PLCTAG_STATUS_OK) {
      fprintf(stdout, "ERROR: Unable to write the data to tag \"%s\"! Got error code %d: %s\n", "FaultMessage", rc, plc_tag_decode_error(rc));
      plc_tag_destroy(tags["FaultMessage"]);
      return 0;
    }
    rc = plc_tag_write(tags["Mode"], g_data_timeout);
    if (rc != PLCTAG_STATUS_OK) {
      fprintf(stdout, "ERROR: Unable to write the data to tag \"%s\"! Got error code %d: %s\n", "Mode", rc, plc_tag_decode_error(rc));
      plc_tag_destroy(tags["Mode"]);
      return 0;
    }
    rc = plc_tag_write(tags["State"], g_data_timeout);
    if (rc != PLCTAG_STATUS_OK) {
      fprintf(stdout, "ERROR: Unable to write the data to tag \"%s\"! Got error code %d: %s\n", "State", rc, plc_tag_decode_error(rc));
      plc_tag_destroy(tags["State"]);
      return 0;
    }
    rc = plc_tag_write(tags["StateName"], g_data_timeout);
    if (rc != PLCTAG_STATUS_OK) {
      fprintf(stdout, "ERROR: Unable to write the data to tag \"%s\"! Got error code %d: %s\n", "StateName", rc, plc_tag_decode_error(rc));
      plc_tag_destroy(tags["StateName"]);
      return 0;
    }
  
  }

  // Delete tags
  plc_tag_destroy(tags["FaultMessage"]);
  plc_tag_destroy(tags["Mode"]);
  plc_tag_destroy(tags["SequenceName"]);
  plc_tag_destroy(tags["SequenceRun"]);
  plc_tag_destroy(tags["State"]);
  plc_tag_destroy(tags["StateName"]);

  fprintf(stdout, "Exiting...\n");
  return 0;
}


