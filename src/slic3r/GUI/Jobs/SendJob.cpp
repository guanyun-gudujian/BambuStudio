#include "SendJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

#define CHECK_GCODE_FAILED_STR      _L("Abnormal print file data. Please slice again.")
#define PRINTJOB_CANCEL_STR         _L("Task canceled.")
#define TIMEOUT_TO_UPLOAD_STR       _L("Upload task timed out. Please check the network status and try again.")
#define FAILED_IN_CLOUD_SERVICE_STR _L("Cloud service connection failed. Please try again.")
#define FILE_IS_NOT_EXISTS_STR      _L("Print file not found. please slice again.")
#define FILE_OVER_SIZE_STR          _L("The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again.")
#define PRINT_CANCELED_STR          _L("Task canceled.")
#define SEND_PRINT_FAILED_STR       _L("Failed to send the print job. Please try again.")
#define UPLOAD_FTP_FAILED_STR       _L("Failed to upload file to ftp. Please try again.")

#define DESC_NETWORK_ERROR      _L("Check the current status of the bambu server by clicking on the link above.")
#define DESC_FILE_TOO_LARGE     _L("The size of the print file is too large. Please adjust the file size and try again.")
#define DESC_FAIL_NOT_EXIST     _L("Print file not found, Please slice it again and send it for printing.")
#define DESC_UPLOAD_FTP_FAILED  _L("Failed to upload print file to FTP. Please check the network status and try again.")

#define SENDING_OVER_LAN_STR   _L("Sending print job over LAN")
#define SENDING_OVER_CLOUD_STR _L("Sending print job through cloud service")

SendJob::SendJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id)
{
    m_print_job_completed_id = plater->get_send_finished_event();
}

void SendJob::prepare()
{
    m_plater->get_print_job_data(&job_data);
    if (&job_data) {
        std::string temp_file = Slic3r::resources_dir() + "/check_access_code.txt";
        auto check_access_code_path = temp_file.c_str();
        job_data._temp_path = fs::path(check_access_code_path);
    }
}

void SendJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &/*e*/) {
        PlaterJob::on_exception(eptr);
    }
}

wxString SendJob::get_http_error_msg(unsigned int status, std::string body)
{
    int code = 0;
    std::string error;
    std::string message;
    wxString result;
    if (status >= 400 && status < 500)
        try {
        json j = json::parse(body);
        if (j.contains("code")) {
            if (!j["code"].is_null())
                code = j["code"].get<int>();
        }
        if (j.contains("error")) {
            if (!j["error"].is_null())
                error = j["error"].get<std::string>();
        }
        if (j.contains("message")) {
            if (!j["message"].is_null())
                message = j["message"].get<std::string>();
        }
    }
    catch (...) {
        ;
    }
    else if (status == 503) {
        return _L("Service Unavailable");
    }
    else {
        wxString unkown_text = _L("Unknown Error.");
        unkown_text += wxString::Format("status=%u, body=%s", status, body);
        return unkown_text;
    }

    BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

    result = wxString::Format("code=%u, error=%s", code, from_u8(error));
    return result;
}

inline std::string get_transform_string(int bytes)
{
	float ms = (float)bytes / 1024.0f / 1024.0f;
	float ks = (float)bytes / 1024.0f;
	char buffer[32];
	if (ms > 0)
		::sprintf(buffer, "%.1fM", ms);
	else if (ks > 0)
		::sprintf(buffer, "%.1fK", ks);
	else
		::sprintf(buffer, "%.1fK", ks);
	return buffer;
}

void SendJob::process()
{
    BBL::PrintParams params;
    wxString msg;
    int curr_percent = 10;
    NetworkAgent* m_agent = wxGetApp().getAgent();
    AppConfig* config = wxGetApp().app_config;
    int result = -1;
    //unsigned int http_code;
    std::string http_body;




    // local print access
    params.dev_ip = m_dev_ip;
    params.username = "bblp";
    params.password = m_access_code;
    params.use_ssl_for_ftp = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt = m_local_use_ssl_for_mqtt;

    // check access code and ip address
    params.dev_id = m_dev_id;
    params.project_name = "verify_job";
    params.filename = job_data._temp_path.string();
    params.connection_type = this->connection_type;

    result = m_agent->start_send_gcode_to_sdcard(params, nullptr, nullptr, nullptr);
    if (result != 0) {
        BOOST_LOG_TRIVIAL(error) << "access code is invalid";
        m_enter_ip_address_fun_fail(result);
        m_job_finished = true;
        return;
    }
    else if(m_is_check_mode && !m_check_and_continue){
        m_enter_ip_address_fun_success();
        m_job_finished = true;
        return;
    }


    /* display info */
    msg = _L("Sending gcode file over LAN");
   /* if (this->connection_type == "lan") {
        msg = _L("Sending gcode file over LAN");
    }
    else {
        msg = _L("Sending gcode file through cloud service");
    }*/

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        if (job_data.plate_idx == PLATE_ALL_IDX) {
            //all plate
            for (int index = 0; index < total_plate_num; index++)
            {
                PartPlate* plate_n = m_plater->get_partplate_list().get_plate(index);
                if (plate_n && plate_n->is_valid_gcode_file())
                {
                    plate = plate_n;
                    break;
                }
            }
        }
        else {
            plate = m_plater->get_partplate_list().get_curr_plate();
        }
        if (plate == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "can not find plate with valid gcode file when sending to print, plate_index="<< job_data.plate_idx;
            update_status(curr_percent, CHECK_GCODE_FAILED_STR);
            return;
        }
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file()) {
        update_status(curr_percent, CHECK_GCODE_FAILED_STR);
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, PRINTJOB_CANCEL_STR);
        return;
    }

    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    params.dev_id               = m_dev_id;
    params.project_name         = m_project_name + ".gcode.3mf";
    params.preset_name          = wxGetApp().preset_bundle->prints.get_selected_preset_name();

    if (wxGetApp().plater()->using_exported_file())
        params.filename = wxGetApp().plater()->get_3mf_filename();
    else
        params.filename = job_data._3mf_path.string();


    params.config_filename      = job_data._3mf_config_path.string();

    params.plate_index          = curr_plate_idx;
    params.ams_mapping          = this->task_ams_mapping;
    params.connection_type      = this->connection_type;
    params.task_use_ams         = this->task_use_ams;

    // local print access
    params.dev_ip = m_dev_ip;
    params.username = "bblp";
    params.password = m_access_code;
    params.use_ssl_for_ftp = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt = m_local_use_ssl_for_mqtt;
    wxString error_text;
    wxString msg_text;

    const int StagePercentPoint[(int)PrintingStageFinished + 1] = {
        20,  // PrintingStageCreate
        30,  // PrintingStageUpload
        99,  // PrintingStageWaiting
        99,  // PrintingStageRecord
        99,  // PrintingStageSending
        100, // PrintingStageFinished
        100  // PrintingStageFinished
    };

    auto update_fn = [this, &msg, &curr_percent, &error_text, StagePercentPoint](int stage, int code, std::string info) {
                        if (stage == SendingPrintJobStage::PrintingStageCreate) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending gcode file over LAN");
                            } else {
                                msg = _L("Sending gcode file to sdcard");
                            }
                        }
                        else if (stage == SendingPrintJobStage::PrintingStageUpload) {
                            if (code >= 0 && code <= 100 && !info.empty()) {
							    if (this->connection_type == "lan") {
								    msg = _L("Sending gcode file over LAN");
							    }
							    else {
								    msg = _L("Sending gcode file to sdcard");
							    }
                                if (!info.empty()) {
                                    msg += wxString::Format("(%s)", info);
                                }
                            }
                        }
						else if (stage == SendingPrintJobStage::PrintingStageFinished) {
                            msg = wxString::Format(_L("Successfully sent. Close current page in %s s"), info);
						}
						else {
							if (this->connection_type == "lan") {
								msg = _L("Sending gcode file over LAN");
							}
							else {
                                msg = _L("Sending gcode file over LAN");
							}
						}

                        // update current percnet
                        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
                            curr_percent = StagePercentPoint[stage];
                            if ((stage == BBL::SendingPrintJobStage::PrintingStageUpload) &&
                                (code > 0 && code <= 100)) {
                                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
                            }
                        }

                        //get errors
                        if (code > 100 || code < 0 || stage == BBL::SendingPrintJobStage::PrintingStageERROR) {
                            if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE) {
                                m_plater->update_print_error_info(code, DESC_FILE_TOO_LARGE.ToStdString(), info);
                            }
                            else if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
                                m_plater->update_print_error_info(code, DESC_FAIL_NOT_EXIST.ToStdString(), info);
                            }
                            else if (code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
                                m_plater->update_print_error_info(code, DESC_UPLOAD_FTP_FAILED.ToStdString(), info);
                            }
                            else {
                                m_plater->update_print_error_info(code, DESC_NETWORK_ERROR.ToStdString(), info);
                            }
                        }
                        else {
                            this->update_status(curr_percent, msg);
                        }
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };


    if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";

        if (!params.password.empty()
            && !params.dev_ip.empty()
            && this->has_sdcard) {
            // try to send local with record
            BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode to printer";
            this->update_status(curr_percent, _L("Sending gcode file over LAN"));
            result = m_agent->start_send_gcode_to_sdcard(params, update_fn, cancel_fn, nullptr);
            if (result == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED) {
                params.comments = "upload_failed";
            } else {
                params.comments = (boost::format("failed(%1%)") % result).str();
            }
            if (result < 0) {
                // try to send with cloud
                BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode file to printer";
                this->update_status(curr_percent, _L("Sending gcode file over LAN"));
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode file to printer";
            this->update_status(curr_percent, _L("Sending gcode file over LAN"));
        }
    } else {
        if (this->has_sdcard) {
            this->update_status(curr_percent, _L("Sending gcode file over LAN"));
            result = m_agent->start_send_gcode_to_sdcard(params, update_fn, cancel_fn, nullptr);
        } else {
            this->update_status(curr_percent, _L("Storage needs to be inserted before sending to printer."));
            return;
        }
    }

    if (was_canceled()) {
        update_status(curr_percent, PRINTJOB_CANCEL_STR);
        return;
    }

    if (result < 0) {
        curr_percent = -1;

        if (result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            msg_text = FILE_IS_NOT_EXISTS_STR;
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE || result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE) {
            msg_text = FILE_OVER_SIZE_STR;
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_WR_CHECK_MD5_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SP_CHECK_MD5_FAILED) {
            msg_text = FAILED_IN_CLOUD_SERVICE_STR;
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_WR_GET_NOTIFICATION_TIMEOUT || result == BAMBU_NETWORK_ERR_PRINT_SP_GET_NOTIFICATION_TIMEOUT) {
            msg_text = TIMEOUT_TO_UPLOAD_STR;
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            msg_text = UPLOAD_FTP_FAILED_STR;
        }
        else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = PRINT_CANCELED_STR;
        }
        else {
            msg_text = SEND_PRINT_FAILED_STR;
        }

        if (result != BAMBU_NETWORK_ERR_CANCELED) {
            this->show_error_info(msg_text, 0, "", "");
        }
        BOOST_LOG_TRIVIAL(error) << "send_job: failed, result = " << result;

    }
    else {
        BOOST_LOG_TRIVIAL(error) << "send_job: send ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        evt->SetString(from_u8(params.project_name));
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void SendJob::on_success(std::function<void()> success)
{
	m_success_fun = success;
}

void SendJob::on_check_ip_address_fail(std::function<void(int)> func)
{
    m_enter_ip_address_fun_fail = func;
}

void SendJob::on_check_ip_address_success(std::function<void()> func)
{
    m_enter_ip_address_fun_success = func;
}


void SendJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

void SendJob::set_project_name(std::string name)
{
    m_project_name = name;
}

}} // namespace Slic3r::GUI
