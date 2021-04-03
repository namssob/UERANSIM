//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#include "task.hpp"
#include <asn/rrc/ASN_RRC_RRCSetupRequest-IEs.h>
#include <asn/rrc/ASN_RRC_RRCSetupRequest.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer.h>
#include <rrc/encode.hpp>
#include <ue/app/task.hpp>
#include <ue/mr/task.hpp>
#include <ue/nas/task.hpp>
#include <ue/sra/task.hpp>
#include <utils/common.hpp>

namespace nr::ue
{

UeRrcTask::UeRrcTask(TaskBase *base) : m_base{base}
{
    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "rrc");

    m_state = ERrcState::RRC_IDLE;
}

void UeRrcTask::onStart()
{
}

void UeRrcTask::onQuit()
{
    // TODO
}

void UeRrcTask::onLoop()
{
    NtsMessage *msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_MR_TO_RRC: {
        auto *w = dynamic_cast<NwUeMrToRrc *>(msg);
        switch (w->present)
        {
        case NwUeMrToRrc::RRC_PDU_DELIVERY: {
            handleDownlinkRrc(w->channel, w->pdu);
            break;
        }
        case NwUeMrToRrc::RADIO_LINK_FAILURE: {
            handleRadioLinkFailure();
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_NAS_TO_RRC: {
        auto *w = dynamic_cast<NwUeNasToRrc *>(msg);
        switch (w->present)
        {
        case NwUeNasToRrc::PLMN_SEARCH_REQUEST: {
            m_base->sraTask->push(new NwUeRrcToSra(NwUeRrcToSra::PLMN_SEARCH_REQUEST));
            break;
        }
        case NwUeNasToRrc::INITIAL_NAS_DELIVERY: {
            deliverInitialNas(std::move(w->nasPdu), w->rrcEstablishmentCause);
            break;
        }
        case NwUeNasToRrc::UPLINK_NAS_DELIVERY: {
            deliverUplinkNas(std::move(w->nasPdu));
            break;
        }
        case NwUeNasToRrc::LOCAL_RELEASE_CONNECTION: {
            m_state = ERrcState::RRC_IDLE;

            auto *wr = new NwUeRrcToMr(NwUeRrcToMr::RRC_CONNECTION_RELEASE);
            wr->cause = rls::ECause::RRC_LOCAL_RELEASE;
            m_base->mrTask->push(wr);

            m_base->nasTask->push(new NwUeRrcToNas(NwUeRrcToNas::RRC_CONNECTION_RELEASE));
            break;
        }
        case NwUeNasToRrc::CELL_SELECTION_COMMAND: {
            auto *wr = new NwUeRrcToSra(NwUeRrcToSra::CELL_SELECTION_COMMAND);
            wr->cellId = w->cellId;
            wr->isSuitableCell = w->isSuitableCell;
            m_base->sraTask->push(wr);
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_SRA_TO_RRC: {
        auto *w = dynamic_cast<NwUeSraToRrc *>(msg);
        switch (w->present)
        {
        case NwUeSraToRrc::PLMN_SEARCH_RESPONSE: {
            auto *wr = new NwUeRrcToNas(NwUeRrcToNas::PLMN_SEARCH_RESPONSE);
            wr->measurements = std::move(w->measurements);
            m_base->nasTask->push(wr);
            break;
        }
        case NwUeSraToRrc::SERVING_CELL_CHANGE: {
            auto *wr = new NwUeRrcToNas(NwUeRrcToNas::SERVING_CELL_CHANGE);
            wr->servingCell = w->servingCell;
            m_base->nasTask->push(wr);
            break;
        }
        }
        break;
    }
    default:
        m_logger->unhandledNts(msg);
        break;
    }

    delete msg;
}

void UeRrcTask::handleRadioLinkFailure()
{
    m_state = ERrcState::RRC_IDLE;
    m_base->nasTask->push(new NwUeRrcToNas(NwUeRrcToNas::RADIO_LINK_FAILURE));
}

} // namespace nr::ue