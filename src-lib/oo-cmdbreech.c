/*
  oo_cmdbreech - breech-loading command processor

  (C)Copyright 2017-2020 Smithee Solutions LLC

  Support provided by the Security Industry Association
  http://www.securityindustry.org

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/


#include <string.h>


#include <jansson.h>


#include <osdp-tls.h>
#include <open-osdp.h>
#include <osdp_conformance.h>


extern OSDP_OUT_CMD current_output_command [];
extern OSDP_PARAMETERS p_card; 

int
  read_command
    (OSDP_CONTEXT *ctx,
    OSDP_COMMAND *cmd)

{ /* read_command */

  unsigned short buffer_length;
  FILE *cmdf;
  char command [1024];
  char current_command [1024];
  char current_options [1024];
  int i;
  char json_string [16384];
  OSDP_MFG_ARGS *mfg_args;
  json_t *parameter;
  json_t *root;
  int set_led_temp;
  json_error_t status_json;
  int status;
  int status_io;
  unsigned char temp_buffer [6];
  unsigned short int temp_buffer_length;
  char *test_command;
  char this_command [1024];
  json_t *value;
  json_t *value2;
  char vstr [1024];


  status = ST_CMD_PATH;
  cmdf = fopen (ctx->command_path, "r");
  if (cmdf != NULL)
  {
    status = ST_OK;
    memset (json_string, 0, sizeof (json_string));
    status_io = fread (json_string,
      sizeof (json_string [0]), sizeof (json_string), cmdf);
    if (status_io >= sizeof (json_string))
      status = ST_CMD_OVERFLOW;
    if (status_io <= 0)
      status = ST_CMD_UNDERFLOW;
  };
  if (status EQUALS ST_OK)
  {
    root = json_loads (json_string, 0, &status_json);
    if (!root)
    {
      fprintf (stderr, "JSON parser failed.  String was ->\n%s<-\n",
        json_string);
      status = ST_CMD_ERROR;
    };
  };
  if (status EQUALS ST_OK)
  {
    status = osdp_command_match(ctx, root, current_command, &(cmd->command));
    if (ctx->verbosity > 3)
      fprintf (stderr, "command was %s\n", current_command);
  };
  switch (cmd->command)
  {
  // command bio_read send bio read template command

  case OSDP_CMDB_BIOREAD:
    cmd->command = OSDP_CMD_BIOREAD;
    break;

  case OSDP_CMDB_BIOMATCH:
    cmd->command = OSDP_CMD_BIOMATCH;
    status = ST_OK;
    break;

  case OSDP_CMDB_FACTORY_DEFAULT:
    fprintf(ctx->log, "***RESET TO FACTORY DEFAULT***\n");
    sprintf(command, "rm -f %s", OSDP_SAVED_PARAMETERS);
    system(command);
    status = ST_OK;
    break;

  case OSDP_CMDB_NOOP:
    // command parser no-op so OK command no-op
    cmd->command = OSDP_CMD_NOOP;
    status = ST_OK;
    break;

  // getpiv object-id=zzzzzz offset=0000 data-element=qq

  case OSDP_CMDB_PIVDATA:
    cmd->command = OSDP_CMDB_PIVDATA;

    // details:
    // first 3 octets are the object id
    // fourth octet is the data element
    // fifth element is 2 bytes, offset into data element.

    parameter = json_object_get(root, "object-id");
    if (json_is_string(parameter))
    {
      strcpy(vstr, json_string_value(parameter));
      temp_buffer_length = sizeof(temp_buffer);
      status = osdp_string_to_buffer(ctx, vstr, temp_buffer, &temp_buffer_length);
      memcpy(cmd->details, temp_buffer, 3); // it's the 3-byte oid
    };
    parameter = json_object_get(root, "data-element");
    if (json_is_string(parameter))
    {
      strcpy (vstr, json_string_value (parameter));
      sscanf (vstr, "%x", &i);
      cmd->details [3] = i;
    };
    parameter = json_object_get(root, "offset");
    if (json_is_string(parameter))
    {
      strcpy(vstr, json_string_value(parameter));
      temp_buffer_length = sizeof(temp_buffer);
      status = osdp_string_to_buffer(ctx, vstr, temp_buffer, &temp_buffer_length);
      memcpy((cmd->details)+4, temp_buffer, 2);
    };
    if (status EQUALS ST_OK)
    {
      status = enqueue_command(ctx, cmd);
      cmd->command = OSDP_CMD_NOOP;
    };
    break;

  // "polling" toggles polling enabled and sets sequence to 0
  // polling action=reset always sends seq 0
  // polling action=resume resumes sequence numbers after next message goes out the door

  case OSDP_CMDB_POLLING:
    {
      parameter = json_object_get(root, "action");
      if (json_is_string(parameter))
      {
        if (0 EQUALS strcmp("reset", json_string_value(parameter)))
        {
          fprintf(ctx->log, "Polling: resetting sequence number to 0\n");
          ctx->next_sequence = 0;
          ctx->enable_poll = OO_POLL_ENABLED;
        };
        if (0 EQUALS strcmp("resume", json_string_value(parameter)))
        {
          fprintf(ctx->log, "Polling: resuming sequence numbering\n");
          ctx->enable_poll = OO_POLL_RESUME;
        };
      }
      else
      {
        // toggle it (used to be just 1 and 0)

        if (ctx->enable_poll EQUALS OO_POLL_ENABLED)
          ctx->enable_poll = OO_POLL_NEVER;
        else
          ctx->enable_poll = OO_POLL_ENABLED;

        ctx->next_sequence = 0;
        fprintf(ctx->log, "enable_polling now %x, sequence reset to 0\n", ctx->enable_poll);
      };

      cmd->command = OSDP_CMD_NOOP;
    };
    break;

  // command reset - reset "link" i.e. sequence number

  case OSDP_CMDB_RESET:
    ctx->next_sequence = 0;
    cmd->command = OSDP_CMD_NOOP;
    break;

  case OSDP_CMDB_TRACE:
    ctx->trace = 1 ^ ctx->trace; // toggle low order bit
    fprintf(ctx->log, "Tracing set to %d\n", ctx->trace);
    cmd->command = OSDP_CMD_NOOP;
    break;

  default:
    if (ctx->verbosity > 3)
      fprintf(stderr, "command not processed in switch (%d.)\n", cmd->command);
    status = ST_OK; // ok to proceed with old way
    break;
  };
  value = json_object_get (root, "command");
  if (!json_is_string (value)) status = ST_CMD_INVALID;
  if (status EQUALS ST_OK)
  {
    strcpy (current_command, json_string_value (value));

    fprintf(ctx->log, "Command %s received.\n", current_command);
  };

  // command acurxsize.  Sends the proper value (as the ACU)

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp(current_command, "acurxsize"))
    {
      cmd->command = OSDP_CMDB_ACURXSIZE;
    };
  };

  // command busy

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "busy"))
    {
      cmd->command = OSDP_CMDB_BUSY;
    };
  };

  // command buzz

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "buzz"))
    {
      cmd->command = OSDP_CMDB_BUZZ;

      // set default on, off, repeat timers

      cmd->details [0] = 15;
      cmd->details [1] = 15;
      cmd->details [2] = 3;

      // also use off_time if it's present

      parameter = json_object_get (root, "off_time");
      if (json_is_string (parameter))
      {
        strcpy (vstr, json_string_value (parameter));
        sscanf (vstr, "%d", &i);
        cmd->details [1] = i;
      };

      // also use on_time if it's present

      parameter = json_object_get (root, "on_time");
      if (json_is_string (parameter))
      {
        strcpy (vstr, json_string_value (parameter));
        sscanf (vstr, "%d", &i);
        cmd->details [0] = i;
      };

      // also use repeat if it's present

      parameter = json_object_get (root, "repeat");
      if (json_is_string (parameter))
      {
        strcpy (vstr, json_string_value (parameter));
        sscanf (vstr, "%d", &i);
        cmd->details [2] = i;
      };
    };
  }; 

  // command capabilities

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "capabilities"))
    {
      cmd->command = OSDP_CMDB_CAPAS;
    };
  }; 

  // COMSET.  takes two option arguments, "new_address" and "new_speed".
  // default for new_address is 0x00, default for new_speed is 9600

  if (status EQUALS ST_OK)
  {

    if (0 EQUALS strcmp (current_command, "comset"))
    {
      cmd->command = OSDP_CMDB_COMSET;

      value = json_object_get (root, "new_address");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        cmd->details [0] = i;
      };
      value = json_object_get (root, "new_speed");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        *(int *) &(cmd->details [4]) = i; // by convention bytes 4,5,6,7 are the speed.
      };
      if (ctx->verbosity > 2)
        fprintf (stderr, "Command COMSET Address %d Speed %d\n",
          (int) (cmd->details [0]),
          *(int *) &(cmd->details [4]));
    };
  }; 

  // command conform_2_2_1

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_2_2_1")) {
      cmd->command = OSDP_CMDB_CONFORM_2_2_1; }; };

  // command conform_2_2_2

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_2_2_2")) {
      cmd->command = OSDP_CMDB_CONFORM_2_2_2; }; };

  // command conform_2_2_3

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_2_2_3")) {
      cmd->command = OSDP_CMDB_CONFORM_2_2_3; }; };

  // command conform_2_2_4

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_2_2_4")) {
      cmd->command = OSDP_CMDB_CONFORM_2_2_4; }; };

  // command conform_2_6_1

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "conform_2_6_1"))
    {
      cmd->command = OSDP_CMDB_CONFORM_2_6_1;
      strcpy (ctx->text,
" ***OSDP CONFORMANCE TEST*** 45678901234567890123456789012345678901234567890123456789012345678901234567890");
    };
  };

  // command conform_2_11_3 - send an ID on the all-stations PD address

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "conform_2_11_3"))
    {
      cmd->command = OSDP_CMDB_CONFORM_2_11_3;
    };
  };


  // command conform_3_14_2 - corrupted COMSET

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "conform_3_14_2"))
    {
      cmd->command = OSDP_CMD_NOOP; // nothing other than what's here so no-op

      status = send_comset (ctx, p_card.addr, 0, "999999");
    };
  };

  // command conform_2-14-3: rogue secure poll

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_2_14_3")) {
      cmd->command = OSDP_CMDB_CONFORM_2_14_3; }; };

  // command conform_3_20_1 - MFG

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "conform_3_20_1")) {
      cmd->command = OSDP_CMDB_CONFORM_3_20_1; }; };

  // command induce-NAK

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp (current_command, "induce-NAK")) {
      cmd->command = OSDP_CMDB_INDUCE_NAK; }; };

  // command keep-active
  // argument is time in milliseconds "milliseconds".  default is 7000;

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "keep-active"))
    {
      int i;
      i = 7000;
      cmd->command = OSDP_CMDB_KEEPACTIVE;
      cmd->details [0] = (i & 0xff); // lsb
      cmd->details [1] = (i/0x100); // msb
      parameter = json_object_get (root, "milliseconds");
      if (json_is_string (parameter))
      {
        strcpy (vstr, json_string_value (parameter));
        sscanf (vstr, "%d", &i);
        cmd->details [0] = (i & 0xff); // lsb
        cmd->details [1] = (i/0x100); // msb
      };
    };
  };


  // command MFG.  Arguments are OUI, command-id, command-specific-data.
  // c-s-d is is 2-hexit bytes, length inferred.

  if (status EQUALS ST_OK) {
    if (0 EQUALS strcmp(current_command, "mfg")) {
      cmd->command = OSDP_CMDB_MFG; 
      mfg_args = (OSDP_MFG_ARGS *)(cmd->details);
      memset(mfg_args, 0, sizeof (*mfg_args));
      parameter = json_object_get(root, "command-id");
      if (json_is_string(parameter))
      {
        int i;
        sscanf(json_string_value(parameter), "%d", &i);
        mfg_args->command_ID = i;
      };
      parameter = json_object_get(root, "command-specific-data");
      if (json_is_string(parameter))
      {
        strcpy(mfg_args->c_s_d, json_string_value(parameter));
      };
      parameter = json_object_get(root, "oui");
      if (json_is_string(parameter))
      {
        strcpy(mfg_args->oui, json_string_value(parameter));
      };
fprintf(stderr, "test: queuing MFG %d\n", cmd->command);
status = enqueue_command(ctx, cmd);
cmd->command = OSDP_CMD_NOOP;
    };
  };

  // command text

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "text"))
    {
      char
        field [1024];
      json_t
        *value;
      strcpy (field, "message");
      value = json_object_get (root, field);
      if (json_is_string (value))
      {
        strcpy (ctx->text, json_string_value (value));
        cmd->command = OSDP_CMDB_TEXT;
      };
    };
  };

  // command transfer

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "transfer"))
    {
      cmd->command = OSDP_CMDB_TRANSFER;
    };

    // if there's a "file" argument use that
    parameter = json_object_get (root, "file");
    if (json_is_string (parameter))
    {
      strcpy ((char *)cmd->details, json_string_value (parameter));
    };
fprintf(stderr, "DEBUG: queuing TRANSFER %d\n", cmd->command);
    status = enqueue_command(ctx, cmd);
    cmd->command = OSDP_CMD_NOOP;
  };

  // command dump_status

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "dump_status";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_DUMP_STATUS;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  /*
    command "genauth"

    example:
      "command" : "genauth"
      "template" : "witness" or "challenge" or "060-24-02" 0r "060-25-02"
      "keyref" : "9E" 
        (or "9e" meaning card auth key - SP800-73-4 Part 1 Page 19 Table 4b.)
      "algoref" : "07"
        (07 is RSA; or 11 for ECC P-256 or 14 for ECC curve P-384 per
        SP800-78-4 Table 6-2 page 12)
      "payload" : "(hex bytes)" (which should be a well-formed Dynamic Authentication Template)
  */
  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "genauth"))
    {
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
      cmd->command = OSDP_CMDB_WITNESS;
      value = json_object_get (root, "template");
      if (json_is_string (value))
      {
        if (0 EQUALS strcmp("060-24-02", json_string_value (value)))
        {
          cmd->command = OSDP_CMDB_CONFORM_060_24_02; // challenge-after-raw
        };
        if (0 EQUALS strcmp("060-25-02", json_string_value (value)))
        {
          cmd->command = OSDP_CMDB_CONFORM_060_25_02; // witness-after-raw
        };
        if (0 EQUALS strcmp("challenge", json_string_value (value)))
        {
          cmd->command = OSDP_CMDB_CHALLENGE;
        };
      };

      // details [0] is algoref

      value = json_object_get (root, "algoref");
      status = ST_OSDP_BAD_GENAUTH_1;
      if (json_is_string (value))
      {
        if (0 EQUALS strcmp("07", json_string_value (value)))
        {
          cmd->details [0] = 0x07;
          status = ST_OK;
        };
      };

      // details [1] is keyref

      value = json_object_get (root, "keyref");
      status = ST_OSDP_BAD_GENAUTH_2;
      if (json_is_string (value))
      {
        int i;

        sscanf(json_string_value(value), "%x", &i);
        cmd->details [1] = (unsigned char)i;
        status = ST_OK;
      };

      // details [2-n] is genauth payload
      // cmd->details_length = 2 + payload length

      value = json_object_get (root, "payload");
      status = ST_OSDP_BAD_GENAUTH_3;
      if (json_is_string (value))
      {
        unsigned short int lth;

        lth = sizeof(cmd->details) - 2;
        status = osdp_string_to_buffer(ctx, (char *)json_string_value(value), cmd->details+2,  &lth);
        cmd->details_length = 2+lth; //algoref, keyref, payload
      };

fprintf(stderr, "DEBUG: enqueue %d\n", cmd->command);
      status = enqueue_command(ctx, cmd);
      cmd->command = OSDP_CMD_NOOP;
    };
  };

  // identify command - send an osdp_ID

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "identify";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_IDENT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
fprintf(stderr, "test: queuing %d\n", cmd->command);
status = enqueue_command(ctx, cmd);
cmd->command = OSDP_CMD_NOOP;
    };
  }; 

  // initiate secure channel

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "initiate-secure-channel";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_INIT_SECURE;
      cmd->details_param_1 = 0;

      parameter = json_object_get(root, "key-slot");
      if (json_is_string (parameter))
      {
        if (0 EQUALS strcmp("1", json_string_value(parameter)))
          cmd->details_param_1 = 1;
      };

      status = enqueue_command(ctx, cmd);
      cmd->command = OSDP_CMD_NOOP;
      if (ctx->verbosity > 3)
      {
        fprintf(ctx->log, "Enqueue: %s %d\n", test_command, cmd->details_param_1);
      };
    };
  }; 

  // command "input_status" - request input status

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "input_status";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_ISTAT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  // command "keypad" - send keyboard input

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "keypad"))
    {
      cmd->command = OSDP_CMDB_KEYPAD;

      value = json_object_get (root, "digits");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        if (strlen (vstr) > 9)
        {
          fprintf (stderr, "Too many digits in keypad input, truncating to first 9\n");
          vstr [9] = 0;
        };
        memcpy (cmd->details, vstr, 9);
      };
    };
  };

  // command "keyset" to send a KEYSET using the supplied key
  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "keyset"))
    {
      cmd->command = OSDP_CMDB_KEYSET;

      value = json_object_get (root, "psk-hex");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        memcpy (cmd->details, vstr, OSDP_KEY_OCTETS*2);
      };
      status = enqueue_command(ctx, cmd);
      cmd->command = OSDP_CMD_NOOP;
      if (ctx->verbosity > 3)
      {
        fprintf(ctx->log, "Enqueue: keyset key %s\n", cmd->details);
      };
    };
  };

  // command "local_status" - request local status

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "local_status";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_LSTAT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  // command "led"

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "led"))
    {
      OSDP_RDR_LED_CTL *led_ctl;

      cmd->command = OSDP_CMDB_LED;
      /*
        "details" is an OSDP_RDR_LED_CTL structure.
        set up details with the default values (and then tune that if there
        are parameters with the command.)
      */
      led_ctl = (OSDP_RDR_LED_CTL *)(cmd->details);
      set_led_temp = 0;
      led_ctl->led           = 0;
      led_ctl->perm_control  = OSDP_LED_SET;
      led_ctl->perm_off_time = 0;
      led_ctl->perm_off_color = OSDP_LEDCOLOR_BLACK;
      led_ctl->perm_on_color  = OSDP_LEDCOLOR_GREEN;
      led_ctl->perm_on_time   = 30;
      led_ctl->reader         = 0;
      led_ctl->temp_control   = OSDP_LED_TEMP_NOP;
      led_ctl->temp_off       = 3;
      led_ctl->temp_off_color = OSDP_LEDCOLOR_GREEN;
      led_ctl->temp_on        = 3;
      led_ctl->temp_on_color  = OSDP_LEDCOLOR_RED;
      led_ctl->temp_timer_lsb = 30;
      led_ctl->temp_timer_msb = 0;

      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);

      value = json_object_get (root, "led-number");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->led = i;
      };
      value = json_object_get (root, "perm-control");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->perm_control = i;
      };
      value = json_object_get (root, "perm-off-time");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->perm_off_time = i;
fprintf(ctx->log, "DEBUG: perf_off_time %d.\n", led_ctl->perm_off_time);
      };
      value = json_object_get (root, "perm-off-color");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->perm_off_color = i;
      };
      value = json_object_get (root, "perm-on-time");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->perm_on_time = i;
      };
      value = json_object_get (root, "perm-on-color");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->perm_on_color = i;
      };
      value = json_object_get (root, "temp-off-color");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->temp_off_color = i;
        if (i > 0)
          set_led_temp = 1;
      };
      value = json_object_get (root, "temp-off-time");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->temp_off = i;
        if (i > 0)
          set_led_temp = 1;
      };
      value = json_object_get (root, "temp-on-time");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->temp_on = i;
        if (i > 0)
          set_led_temp = 1;
      };
      value = json_object_get (root, "temp-on-color");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->temp_on_color = i;
        if (i > 0)
          set_led_temp = 1;
      };
      value = json_object_get (root, "temp-timer");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        led_ctl->temp_timer_lsb = i & 0xff;
        led_ctl->temp_timer_msb = i >> 8;
        if (i > 0)
          set_led_temp = 1;
      };

      // lastly look for "temp-control".  If it's set it overrides the
      // implicit temp-control from other values being set.

      value = json_object_get (root, "temp-control");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        set_led_temp = 0; // NOT the implied control
        led_ctl->temp_control = i;
      };
      if (set_led_temp)
      {
        led_ctl->temp_control = OSDP_LED_TEMP_SET;
      };
    };
  }; 
  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "operator_confirm"))
    {
      cmd->command = OSDP_CMD_NOOP; // nothing other than what's here so no-op
      value = json_object_get (root, "test");
      if (json_is_string (value))
      {
        strcpy (current_options, json_string_value (value));
        status = osdp_conform_confirm (current_options);
      };
    };
  };

  // output (digital bits out)

  if (status EQUALS ST_OK)
  {
    int
      i;
    char
      vstr [1024];

    test_command = "output";
    if (0 EQUALS strcmp (current_command, test_command))
    {
      cmd->command = OSDP_CMDB_OUT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);

      // default values in case some are missing

      current_output_command [0].output_number = 0;
      current_output_command [0].control_code = 2; // permanent on immediate
      current_output_command [0].timer = 0; // forever

      // the output command takes arguments: output_number, control_code

      value = json_object_get (root, "output-number");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        current_output_command [0].output_number = i;
fprintf(stderr, "DEBUG: output-number set to %d\n", 
  current_output_command [0].output_number);
      };
      value = json_object_get (root, "control-code");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        current_output_command [0].control_code = i;
      };
      value = json_object_get (root, "timer");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        current_output_command [0].timer = i;
      };
    };
  }; 

  // request output status

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "output_status"))
    {
      cmd->command = OSDP_CMDB_OSTAT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  // present_card - provide card data for osdp_RAW response

  if (status EQUALS ST_OK)
  {
    json_t *option;

    value = json_object_get (root, "command");

    strcpy (this_command, json_string_value (value));
    test_command = "present_card";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_PRESENT_CARD;
      if (ctx->verbosity > 3)
        fprintf(ctx->log, "Command %s submitted\n", test_command);

      // if no options are given, use preset value
      cmd->details_length = p_card.value_len;
      cmd->details_param_1 = p_card.bits;
      memcpy(cmd->details, p_card.value, p_card.value_len);

      // if there's a "raw" option it's the data to use.  bits are also specified.

      option = json_object_get (root, "raw");
      if (json_is_string (option))
      {
        strcpy (vstr, json_string_value (option));
        buffer_length = sizeof(cmd->details);
        status = osdp_string_to_buffer(ctx, vstr, cmd->details, &buffer_length);
        cmd->details_length = buffer_length;
        cmd->details_param_1 = 26;  // assume 26 bits unless otherwise specified
      };

      option = json_object_get (root, "bits");
      if (json_is_string (option))
      {
        strcpy (vstr, json_string_value (option));
        sscanf(vstr, "%d", &i);
        cmd->details_param_1 = i;
      };

      // format can be specified (raw or p/data/p)
      option = json_object_get (root, "format");
      if (json_is_string (option))
      {
        strcpy (vstr, json_string_value (option));
        if (0 EQUALS strcmp(vstr, "p-data-p"))
          ctx->card_format = 1;
        else
          ctx->card_format = 0;
      };

      if (ctx->verbosity > 3)
        if (cmd->details_length > 0)
          fprintf(ctx->log, "present_card: raw (%d. bytes, %d. bits, fmt %d): %s\n",
            cmd->details_length, cmd->details_param_1, ctx->card_format, vstr);
    };
  }; 

  // request (attached) reader status

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "reader_status";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_RSTAT;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "reset_power";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_RESET_POWER;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 
  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "send_poll";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_SEND_POLL;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 
  if (status EQUALS ST_OK)
  {
    test_command = "stop";
    if (0 EQUALS strncmp(json_string_value(value), test_command, strlen(test_command)))
      cmd->command = OSDP_CMDB_STOP;
  };
  if (status EQUALS ST_OK)
  {
    strcpy (this_command, json_string_value (value));
    test_command = "tamper";
    if (0 EQUALS strncmp (this_command, test_command, strlen (test_command)))
    {
      cmd->command = OSDP_CMDB_TAMPER;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);
    };
  }; 

  // command verbosity
  // arg level - range 0-9

  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "verbosity"))
    {
      int
        i;
      char
        vstr [1024];

      cmd->command = OSDP_CMD_NOOP; // nothing other than what's here so no-op
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);

      value = json_object_get (root, "level");
      if (json_is_string (value))
      {
        strcpy (vstr, json_string_value (value));
        sscanf (vstr, "%d", &i);
        ctx->verbosity = i;
        if (ctx->verbosity > 3)
          ctx->trace = 1; // turn on tracing (should be stricter about low-order bit.)
        else
          ctx->trace = 0; // turn off tracing (should be stricter about low-order bit.)
      };
    };
  }; 

  // command "xwrite"
  /*
    example:
      { "command" : "xwrite", "action" : "get-mode" }
  */
  if (status EQUALS ST_OK)
  {
    if (0 EQUALS strcmp (current_command, "xwrite"))
    {
      cmd->command = OSDP_CMDB_XWRITE;
      if (ctx->verbosity > 3)
        fprintf (stderr, "command was %s\n",
          this_command);

      value = json_object_get (root, "action");
      if (json_is_string (value))
      {
        if (0 EQUALS strcmp(json_string_value(value), "get-mode"))
        {
          cmd->details [0] = 1; // 1 in byte 0 is get-mode
        };
        if (0 EQUALS strcmp(json_string_value(value), "scan"))
        {
          cmd->details [0] = 3; // 3 in byte 0 is scan (for smart card)
        };
        if (0 EQUALS strcmp(json_string_value(value), "set-mode"))
        {
          cmd->details [0] = 2; // 2 in byte 0 is set-mode
        };
        if (0 EQUALS strcmp(json_string_value(value), "set-zero"))
        {
          cmd->details [0] = 4; // 4 in byte 0 is set mode 0
        };
        if (0 EQUALS strcmp(json_string_value(value), "done"))
        {
          cmd->details [0] = 5;
        };
        if (0 EQUALS strcmp(json_string_value(value), "apdu"))
        {
          unsigned short int payload_length;
          char payload_value [1024];

          cmd->details [0] = 6;

          // if there's a "payload" fill it in after the command in details

          value2 = json_object_get (root, "payload");
          if (json_is_string (value2))
          {
            payload_length = sizeof(cmd->details);
            strcpy(payload_value, json_string_value(value2));
            status = osdp_string_to_buffer
              (ctx, payload_value, cmd->details+3, &payload_length);
            *(short int *)(cmd->details+1) = payload_length;
          };
        };
      };
fprintf(stderr, "test: queuing XWR %d\n", cmd->command);
status = enqueue_command(ctx, cmd);
cmd->command = OSDP_CMD_NOOP;
    };
  }; 

  if (cmdf != NULL)
    fclose (cmdf);
  if (status != ST_OK)
    fprintf(stderr, "Status %d at read_command.\n", status);
  return (status);

} /* read_command */

