/*!
 * @file jackdriver.cpp
 * @brief Implements the JackDriver QObject class.
 *
 * @section LICENSE
 *
 *      Copyright 2009, 2010, 2011 <qmidiarp-devel@lists.sourceforge.net>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 *
 */

#include "jackdriver.h"
#include "config.h"
#include <stdio.h>


JackDriver::JackDriver(
    int p_portCount,
    void * callback_context,
    void (* p_tr_state_cb)(bool j_tr_state, void * context),
    bool (* midi_event_received_callback)(void * context, MidiEvent ev),
    void (* tick_callback)(void * context, bool echo_from_trig))
    : DriverBase(callback_context, midi_event_received_callback, tick_callback, 60e9)
{
    transportState = JackTransportStopped;
    portCount = p_portCount;
    cbContext = callback_context;
    trStateCb = p_tr_state_cb;
    jackRunning = false;

    internalTempo = 120;

/** Initialize and activate Jack with out_port_count ports if we use
 *  JACK driver backend, i.e. portCount > 0 */
    if (portCount) {
        callJack(portCount);
        jSampleRate = jack_get_sample_rate(jack_handle);
        setTransportStatus(false);
    }
}
void JackDriver::callJack(int port_count)
{
    if (port_count == -1 && jackRunning) {
        deactivateJack();
        if (jack_handle != 0) {
            jack_client_close(jack_handle);
            jack_handle = 0;
        }
    }
    else if (port_count != -1) {
        if (initJack(port_count)) {
            emit j_shutdown();
        }
        else if (activateJack()) {
            emit j_shutdown();
        }
    }
}

JackDriver::~JackDriver()
{
    if (jackRunning) {
        deactivateJack();
    }
    if (jack_handle != 0) {
        jack_client_close(jack_handle);
        jack_handle = 0;
    }
}

int JackDriver::initJack(int out_port_count)
{
    char buf[16];

    if ((jack_handle = jack_client_open(PACKAGE, JackNullOption, NULL)) == 0) {
        qCritical("jack server not running?");
        return 1;
    }

    jack_on_shutdown(jack_handle, jack_shutdown, (void *)this);

    jack_set_process_callback(jack_handle, process_callback, (void *)this);

    qWarning("jack process callback registered");

    if (!out_port_count) return(0);

    // register JACK MIDI input port
    if ((in_port = jack_port_register(jack_handle, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0)) == 0) {
        qCritical("Failed to register JACK MIDI input port.");
        return 1;
    }

    // register JACK MIDI output ports
    for (int l1 = 0; l1 < out_port_count; l1++)
    {
      snprintf(buf, sizeof(buf), "out %d", l1 + 1);
      if ((out_ports[l1] = jack_port_register(jack_handle, buf, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0)) == 0)
      {
        qCritical("Failed to register JACK MIDI output port.");
        return 1;
      }
    }

    return(0);
}

int JackDriver::activateJack()
{
    if (jack_activate(jack_handle)) {
        qWarning("cannot activate client");
        jackRunning = false;
        return(1);
    }

    jackRunning = true;
    return(0);
}

int JackDriver::deactivateJack()
{
    if (!jackRunning) return(0);

    if (jack_deactivate(jack_handle)) {
        qWarning("cannot deactivate client");
        return(1);
    }
    jackRunning = false;
    qWarning("jack client deactivated");
    return(0);
}

void JackDriver::jack_shutdown(void *arg)
{
    JackDriver *rd = (JackDriver *) arg;
    rd->setJackRunning(false);

    qWarning("JACK shut down. JACK sync Disabled.");
    emit rd->j_shutdown();
}

int JackDriver::process_callback(jack_nframes_t nframes, void *arg)
{
    uint i;
    uint l1, l2, size;

    JackDriver *rd = (JackDriver *) arg;
    uint out_port_count = rd->portCount;
    rd->jackTrCheckState();

    if (!out_port_count) return (0);

    int cur_tempo = rd->tempo;
    uint64_t cur_j_frame = rd->curJFrame;
    bool forward_unmatched = rd->forwardUnmatched;
    int port_unmatched = rd->portUnmatched;

    uint nexttick = 0;
    uint tmptick = 0;
    uint idx = 0;
    int evport;
    uint64_t ev_jframe, ev_sample;
    uint ev_inframe;
    MidiEvent inEv;
    inEv.type = 0;
    inEv.data = 0;
    inEv.channel = 0;
    inEv.value = 0;
    MidiEvent outEv;
    outEv.channel = 0;


    unsigned char* buffer;
    jack_midi_event_t in_event;
    jack_nframes_t event_index = 0;
    jack_nframes_t j_sample_rate = rd->jSampleRate;
    void *in_buf = jack_port_get_buffer(rd->in_port, nframes);
    void *out_buf[out_port_count];
    for (l1 = 0; l1 < out_port_count; l1++) {
        out_buf[l1] = jack_port_get_buffer(rd->out_ports[l1], nframes);
    }
    for (l1 = 0; l1 < out_port_count; l1++) {
        jack_midi_clear_buffer(out_buf[l1]);
    }

    jack_nframes_t event_count = jack_midi_get_event_count(in_buf);
    jack_midi_event_get(&in_event, in_buf, 0);

    for(i = 0; i < nframes; i++) {

        /** MIDI Output queue first **/
        size = rd->evTickQueue.size();
        if (size) { /** If we have events, find earliest event tick **/
            idx = 0;
            nexttick = rd->evTickQueue.head();
            for (l1 = 0; l1 < size; l1++) {
                tmptick = rd->evTickQueue.at(l1);
                if (nexttick > tmptick) {
                    idx = l1;
                    nexttick = tmptick;
                }
            }
            ev_sample = (uint64_t)j_sample_rate * 60 * nexttick / (TPQN * cur_tempo);
            ev_jframe = ev_sample / nframes;
            ev_inframe = ev_sample % nframes;
            if ((ev_jframe <= cur_j_frame) && (ev_inframe <= i)) {
                //qWarning("nexttick %d, ev_frame %d, ev_inframe %d, cur_jframe %d", nexttick, ev_jframe, ev_inframe, cur_j_frame);
                outEv = rd->evQueue.takeAt(idx);
                evport = rd->evPortQueue.takeAt(idx);
                rd->evTickQueue.removeAt(idx);
                int k = 0;
                if ((ev_jframe) <= cur_j_frame) {
                    do {
                        if ((ev_jframe) < cur_j_frame) {
                            ev_inframe = 0;
                        }
                        buffer = jack_midi_event_reserve(out_buf[evport], ev_inframe + k, 3);
                        k++;
                    } while (buffer == NULL);

                    buffer[2] = outEv.value;        /** velocity / value **/
                    buffer[1] = outEv.data;         /** note / controller **/
                    if (outEv.type == EV_NOTEON) buffer[0] = 0x90;
                    if (outEv.type == EV_CONTROLLER) buffer[0] = 0xb0;
                    buffer[0] += outEv.channel;
                }
            }
        }
        /** MIDI Input handling **/
        while ((in_event.time == i) && (event_index < event_count)) {

            if( ((*(in_event.buffer) & 0xf0)) == 0x90 ) {
                inEv.type = EV_NOTEON;
                inEv.value = *(in_event.buffer + 2);
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0x80 ) {
                inEv.type = EV_NOTEON;
                inEv.value = 0;
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0xa0 ) {
                inEv.type = EV_KEYPRESS;
                inEv.value = *(in_event.buffer + 2);
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0xb0 ) {
                inEv.type = EV_CONTROLLER;
                inEv.value = *(in_event.buffer + 2);
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0xc0 ) {
                inEv.type = EV_PGMCHANGE;
                inEv.value = *(in_event.buffer + 1);
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0xd0 ) {
                inEv.type = EV_CHANPRESS;
                inEv.value = *(in_event.buffer + 1);
            }
            else if( ((*(in_event.buffer)) & 0xf0) == 0xe0 ) {
                inEv.type = EV_PITCHBEND;
                inEv.value = *(in_event.buffer + 2) * 128;
                inEv.value += *(in_event.buffer + 1);
                inEv.value -= 8192;
            }
            else inEv.type = EV_NONE;

            inEv.data = *(in_event.buffer + 1);
            inEv.channel = (*(in_event.buffer)) & 0x0f;
            bool unmatched = rd->midi_event_received(inEv);

            if (unmatched && forward_unmatched) {
                buffer = jack_midi_event_reserve(out_buf[port_unmatched], i, in_event.size);
                if (buffer) {
                    for (l2 = 0; l2 < in_event.size; l2++) {
                        buffer[l2] = *(in_event.buffer + l2);
                    }
                }
            }

            event_index++;
            if(event_index < event_count)
                jack_midi_event_get(&in_event, in_buf, event_index);
        }
    }
    rd->handleEchoes();
    return(0);
}

void JackDriver::jackTrCheckState()
{
    int state = getState();

    if (transportState == state) return;

    transportState = state;
    switch (state){
        case JackTransportStopped:
            trStateCb(false, cbContext);
            qWarning( "[JackTransportStopped]" );
        break;

        case JackTransportRolling:
            trStateCb(true, cbContext);
            qWarning( "[JackTransportRolling]" );
        break;

        case JackTransportStarting:
            qWarning( "[JackTransportStarting]" );
        break;

        case JackTransportLooping:
            qWarning( "[JackTransportLooping]" );
        break;
        default:
        break;
    }
}

jack_transport_state_t JackDriver::getState()
{
    return jack_transport_query(jack_handle, &currentPos);
}

void JackDriver::setJackRunning(bool on)
{
    jackRunning = on;
}

jack_position_t JackDriver::getCurrentPos()
{
    return currentPos;
}

void JackDriver::sendMidiEvent(MidiEvent ev, int n_tick, unsigned outport, unsigned duration)
{
  //qWarning("sendMidiEvent([%d, %d, %d, %d], %u, %u) at tick %d", ev.type, ev.channel, ev.data, ev.value, outport, duration, n_tick);
    evQueue.append(ev);
    evTickQueue.append(n_tick);
    evPortQueue.append(outport);

    if ((ev.type == EV_NOTEON) && (ev.value)) {
        ev.value = 0;
        evQueue.append(ev);
        evTickQueue.append(n_tick + duration / 4);
        evPortQueue.append(outport);
    }
}

bool JackDriver::requestEchoAt(int echo_tick, bool echo_from_trig)
{
    if ((echo_tick == (int)lastSchedTick) && (echo_tick)) return false;
    echoTickQueue.append(echo_tick);
    lastSchedTick = echo_tick;
    if (echo_from_trig) tick_callback(true);

    return true;

}

void JackDriver::handleEchoes()
{
    curJFrame++;

    if (!transportState) return;

    int l1;
    int size = echoTickQueue.size();
    int nexttick, tmptick, idx;

    m_current_tick = ((uint64_t)currentPos.frame * TPQN * tempo
            / (currentPos.frame_rate * 60)) - jackOffsetTick;

    if (!size) return;

    idx = 0;
    nexttick = echoTickQueue.head();

    for (l1 = 0; l1 < size; l1++) {
        tmptick = echoTickQueue.at(l1);
        if (nexttick > tmptick) {
            idx = l1;
            nexttick = tmptick;
        }
    }
    if (m_current_tick >= echoTickQueue.at(idx)) {
        echoTickQueue.removeAt(idx);
        tick_callback(false);
    }
}

void JackDriver::setTransportStatus(bool on)
{
    jack_position_t jpos = getCurrentPos();
    if (jpos.beats_per_minute > 0.01)
        tempo = (int)jpos.beats_per_minute;
    else
        tempo = internalTempo;

    jackOffsetTick = (long)jpos.frame * TPQN
        * tempo / (jpos.frame_rate * 60);

    m_current_tick = 0;

    if (on) {
        curJFrame = 0;
        lastSchedTick = 0;
        echoTickQueue.clear();
        evQueue.clear();
        evTickQueue.clear();
        evPortQueue.clear();
        requestEchoAt(0);
    }
    queueStatus = on;
}
