#include "A76XX.h"

A76XX::A76XX(Stream& serial)
    : _serial(serial) {}

bool A76XX::init() {
    // wait until modem is ready
    while (true) {
        sendCMD("AT+CPIN?");
        if (waitResponse("+CPIN: READY", 9000) == Response_t::A76XX_RESPONSE_MATCH_1ST)
            break;
    }

    // turn off echoing commands
    sendCMD("ATE0");
    A76XX_RESPONSE_ASSERT_BOOL(waitResponse(120000));

    // disable reporting mobile equipment errors with numeric values
    sendCMD("AT+CMEE=0");
    A76XX_RESPONSE_ASSERT_BOOL(waitResponse());

    // disable unsolicited codes for time zone change
    sendCMD("AT+CTZR=0");
    A76XX_RESPONSE_ASSERT_BOOL(waitResponse());

    // enable automatic time and time zone updates via NITZ
    sendCMD("AT+CTZU=1");
    A76XX_RESPONSE_ASSERT_BOOL(waitResponse());

    return true;
}

    // connect to network via apn
    bool connect(const char* apn) {
        // define PDP context
        sendCMD("AT+CGDCONT=1,\"IP\",\"", apn, "\"");
        A76XX_RESPONSE_ASSERT_BOOL(waitResponse(9000));

        // activate PDP context
        sendCMD("AT+CGACT=1,1");
        A76XX_RESPONSE_ASSERT_BOOL(waitResponse(9000));

        return true;
    }

    // disconnect - this does not seem to work
    bool disconnect() {
        sendCMD("AT+CGACT=0,1");
        A76XX_RESPONSE_ASSERT_BOOL(waitResponse(9000));
        return true;
    }

    // check context has been activated
    bool isConnected() {
        sendCMD("AT+CGACT?");
        Response_t rsp = waitResponse("+CGACT: 1,1", 9000);
        serialClear();
        return rsp == Response_t::A76XX_RESPONSE_MATCH_1ST ? true : false;
    }

    /// @brief CREG - Get registration status
    /// @return -1 on error
    int getRegistrationStatus() {
        sendCMD("AT+CREG?");
        if (waitResponse("+CREG: ", 9000) == Response_t::A76XX_RESPONSE_MATCH_1ST) {
            _serial.find(',');
            return _serialParseIntClear();
        }
        return -1; // error
    }

    // check we are registered on network or roaming
    bool isRegistered() {
        int stat = getRegistrationStatus();
        return (stat == 1 | stat == 5);
    }

    bool waitForRegistration(uint32_t timeout = 60000) {
        uint32_t tstart = millis();
        while (millis() - tstart < timeout) {
            if (isRegistered())
                return true;
            delay(200);
        }
        return false;
    }

    /// @brief CNSMOD - Show network system mode
    /// @return -1 on error
    int getNetworkSystemMode() {
        sendCMD("AT+CNSMOD?");
        Response_t rsp = waitResponse("+CNSMOD: ");
        if (rsp == Response_t::A76XX_RESPONSE_MATCH_1ST) {
            _serial.find(',');
            return _serialParseIntClear();
        }
        return -1; // error
    }

    //////////////////////////////////////////////////////////////////////////////////////
    // Power functions
    //////////////////////////////////////////////////////////////////////////////////////

    // CRESET
    bool reset() {
        sendCMD("AT+CRESET");
        return waitResponse(9000) == Response_t::A76XX_RESPONSE_OK;
    }

    // CPOF
    bool powerOff() {
        sendCMD("AT+CPOF");
        return waitResponse(9000) == Response_t::A76XX_RESPONSE_OK;
    }

    // CFUN - radio off/on
    bool setPhoneFunctionality(uint8_t fun, bool reset = false) {
        sendCMD("AT+CFUN=", fun, reset ? "1" : "");
        return waitResponse(9000) == Response_t::A76XX_RESPONSE_OK;
    }

    bool radioOFF() { return setPhoneFunctionality(4, false); }
    bool radioON()  { return setPhoneFunctionality(1, false); }

    // restart
    bool restart() {
        if (reset() == false)
            return false;
        return init();
    }

    // enable UART sleep
    bool sleep() {
        sendCMD("AT+CSCLK=2");
        return waitResponse() == Response_t::A76XX_RESPONSE_OK;
    }

    // in mode 2 wake up the module by sending data through the serial port
    bool wakeUp() {
        sendCMD("AT");
        return waitResponse() == Response_t::A76XX_RESPONSE_OK;
    }

    //////////////////////////////////////////////////////////////////////////////////////
    // Modem version functions
    //////////////////////////////////////////////////////////////////////////////////////
    String modelIdentification() {
        // check the command works
        sendCMD("AT+CGMM");
        if (waitResponse() != Response_t::A76XX_RESPONSE_OK) { return ""; }

        // send again
        sendCMD("AT+CGMM");
        String out = "";

        // go at the start of the version number
        _serial.find('\n');

        uint32_t tstart = millis();
        while (millis() - tstart < 5000) {
            if (_serial.available() > 0) {
                char c = static_cast<char>(_serial.read());
                if (c == '\r') {
                    break;
                }
                out += c;
            }
        }
        serialClear();
        return out;
    }


    String revisionIdentification() {
        sendCMD("AT+CGMR");
        if (waitResponse("+CGMR: ") != Response_t::A76XX_RESPONSE_MATCH_1ST) {
            return "";
        }
        String out = "";
        uint32_t tstart = millis();
        while (millis() - tstart < 5000) {
            if (_serial.available() > 0) {
                char c = static_cast<char>(_serial.read());
                if (c == '\r') {
                    break;
                }
                out += c;
            }
        }
        serialClear();
        return out;
    }

    //////////////////////////////////////////////////////////////////////////////////////
    /// Time commands
    //////////////////////////////////////////////////////////////////////////////////////
    // 0 Operation succeeded
    // 1 Unknown error
    // 2 Wrong parameter
    // 3 Wrong date and time calculated 
    // 4 Network error
    // 5 Time zone error 
    // 6 Time out error
    int syncTime(const char* host = "pool.ntp.org", int8_t timezone = 0) {
        Response_t rsp;

        sendCMD("AT+CNTP=\"", host, "\",", timezone);
        rsp = waitResponse();
        if (rsp != Response_t::A76XX_RESPONSE_OK) {
            return A76XX_GENERIC_ERROR;
        }

        sendCMD("AT+CNTP");
        rsp = waitResponse("+CNTP: ", 10000, false, true);
        switch (rsp) {
            case Response_t::A76XX_RESPONSE_MATCH_1ST : {
                return _serial.parseInt();
            }
            case Response_t::A76XX_RESPONSE_TIMEOUT : {
                return A76XX_OPERATION_TIMEDOUT;
            }
            case Response_t::A76XX_RESPONSE_ERROR : {
                return A76XX_GENERIC_ERROR;
            }
        }
    }

    bool readTime(int* year, int* month, int* day, int* hour, int* minute, int* second, int* timezone) {
        sendCMD("AT+CCLK?");
        if (waitResponse("+CCLK: \"") == Response_t::A76XX_RESPONSE_MATCH_1ST) {
            // example response: +CCLK: "14/01/01,02:14:36+08"
            *year   = _serial.parseInt() + 2000; _serial.find("/");
            *month  = _serial.parseInt()       ; _serial.find("/");
            *day    = _serial.parseInt()       ; _serial.find(",");
            *hour   = _serial.parseInt()       ; _serial.find(":");
            *minute = _serial.parseInt()       ; _serial.find(":");
            *second = _serial.parseInt()       ; _serial.find(":");

            if (_serial.read() == '-') { 
                *timezone = - static_cast<float>(_serial.parseInt());
            } else {
                *timezone = + static_cast<float>(_serial.parseInt());
            }

            // clear up
            waitResponse();
     
            return true;
        }
        return false;
    }

    /*
        @brief Get unix time.
    */
    uint32_t getUnixTime(bool UTC = true) {
        int year, month, day, hour, minute, second, timezone;
        if (readTime(&year, &month, &day, &hour, &minute, &second, &timezone) == false) {
            return 0;
        }

        struct tm ts = {0};         // Initalize to all 0's
        ts.tm_year = year - 1900;   // This is year-1900, so for 2023 => 123
        ts.tm_mon  = month - 1;     // january is 0, not 1
        ts.tm_mday = day;
        ts.tm_hour = hour;
        ts.tm_min  = minute;
        ts.tm_sec  = second;

        uint32_t time = mktime(&ts);
        if (UTC == true) {
            time -= static_cast<uint32_t>(timezone*15*60);
        }
        return time;
    }

    // "yy/MM/dd,hh:mm:ss±zz"
    bool getDateTime(String& date_time) {
        sendCMD("AT+CCLK?");
        if (waitResponse("+CCLK: \"") == Response_t::A76XX_RESPONSE_MATCH_1ST) {
            for (int i = 0; i < 20; i++) {
                while (_serial.available() == 0 ){}
                date_time += static_cast<char>(_serial.read());
            }
            // clean up
            waitResponse();
            return true;
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////////////////
    // consume the data from the serial port until the string
    // `match` is found or before `timeout` ms have elapsed
    // if match_OK and match_ERROR are true, we also check if
    // the response matches with the OK and ERROR strings, but
    // precedence is given to `match`, which might be helpful
    // with some commands

    // match zero arguments - only match OK and ERROR
    Response_t waitResponse(int timeout=1000, bool match_OK=true, bool match_ERROR=true) {
        return waitResponse("_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_1_",
                            "_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_2_",
                            "_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_3_",  timeout, match_OK, match_ERROR);
    }

    // match one argument plus OK and ERROR
    Response_t waitResponse(const char* match_1, int timeout=1000, bool match_OK=true, bool match_ERROR=true) {
        return waitResponse(match_1,
                            "_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_2_",
                            "_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_3_",  timeout, match_OK, match_ERROR);
    }

    // match two arguments plus OK and ERROR
    Response_t waitResponse(const char* match_1, const char* match_2, int timeout=1000, bool match_OK=true, bool match_ERROR=true) {
        return waitResponse(match_1, match_2,
                            "_ThIs_Is_AlMoSt_NeVeR_GoNnA_MaTcH_3_",  timeout, match_OK, match_ERROR);
    }

    // match three arguments plus OK and ERROR
    Response_t waitResponse(const char* match_1, const char* match_2, const char* match_3, int timeout=1000, bool match_OK=true, bool match_ERROR=true) {
        // store data here
        String data; data.reserve(64);

        // start timer
        auto tstart = millis();

        while (millis() - tstart < timeout) {
            if (_serial.available() > 0) {
                data += static_cast<char>(_serial.read());
                if (data.endsWith(match_1) == true)
                    return Response_t::A76XX_RESPONSE_MATCH_1ST;
                if (data.endsWith(match_2) == true)
                    return Response_t::A76XX_RESPONSE_MATCH_2ND;
                if (data.endsWith(match_3) == true)
                    return Response_t::A76XX_RESPONSE_MATCH_3RD;
                if (match_ERROR && data.endsWith(RESPONSE_ERROR) == true)
                    return Response_t::A76XX_RESPONSE_ERROR;
                if (match_OK && data.endsWith(RESPONSE_OK) == true)
                    return Response_t::A76XX_RESPONSE_OK;

            }
        }

        return Response_t::A76XX_RESPONSE_TIMEOUT;
    }

    // send command
    template <typename... ARGS>
    void sendCMD(ARGS... args) {
        serialPrint(args..., "\r\n");
        _serial.flush();
    }

  protected:

    // parse an int and then consume all data available in the stream
    int _serialParseIntClear() {
        int retcode = _serial.parseInt(); serialClear();
        return retcode;
    }

    // consume all data available in the stream
    void serialClear() {
        waitResponse();
    }

    // write to stream
    template <typename ARG>
    void serialPrint(ARG arg) {
        _serial.print(arg);
    }

    // write to stream
    template <typename HEAD, typename... TAIL>
    void serialPrint(HEAD head, TAIL... tail) {
        _serial.print(head);
        serialPrint(tail...);
    }