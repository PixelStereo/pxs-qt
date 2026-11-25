cmake_minimum_required(VERSION 3.21)
project(pxs VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PXS_BUILD_TESTS "Build the pxs tests" ${PROJECT_IS_TOP_LEVEL})

# Build identity helpers (pxs_build_suffix, pxs_nearest_release): include(<pxs>/cmake/PixelStereoBuildId.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network)
find_package(Threads REQUIRED)
find_package(Qt6 6.4 QUIET COMPONENTS WebSockets)
if(PXS_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

# The portable wire formats (OSC, DMX, WebSocket frames) live in pxs-core, a submodule of this repository. This repository
# keeps the Qt-facing API; each target below is an adapter over a pxs-core module, and nothing of pxs-core leaks into its
# headers. An application that already added pxs-core itself shares that one (a second copy would define the targets twice).
if(NOT TARGET pxs-core::osc)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pxs-core/CMakeLists.txt")
        message(FATAL_ERROR "third_party/pxs-core is empty. It is a submodule of this repository's own submodule: run\n"
                            "  git submodule update --init --recursive\n"
                            "(or clone with --recurse-submodules).")
    endif()
    add_subdirectory(third_party/pxs-core EXCLUDE_FROM_ALL)
endif()

# OSC 1.0 codec: messages, bundles, address patterns. Qt Core only.
add_library(pxs_osc STATIC src/osc/OscCodec.cpp src/osc/OscCodec.h src/osc/WebSocketFrame.cpp src/osc/WebSocketFrame.h) # the codec of OSC, and the frames of a WebSocket
add_library(pxs::osc ALIAS pxs_osc)
target_include_directories(pxs_osc PUBLIC src/osc)
target_link_libraries(pxs_osc PUBLIC Qt6::Core PRIVATE pxs-core::osc pxs-core::websocket)

# The bridge to pxs-core's portable types (QVariant <-> Value, QString <-> UTF-8, QJson <-> JSON text, the OSC message of the Qt API <-> pxs-core's):
# what an application that moves to pxs::core::tree uses at its edges. Its header names pxs-core's types, so pxs-core is PUBLIC here.
add_library(pxs_convert STATIC src/convert/Convert.cpp src/convert/pxs/Convert.h)
add_library(pxs::convert ALIAS pxs_convert)
target_include_directories(pxs_convert PUBLIC src/convert)
target_link_libraries(pxs_convert PUBLIC pxs::osc pxs-core::tree pxs-core::value pxs-core::osc Qt6::Core)

# Parameters and the address space live in pxs-core (pxs::core::tree: no Qt). This is their Qt face, for an application written on Qt types:
# Parameter, ParamInfo and ChangeFeed with QString / QVariant / QJsonValue at the edge, one implementation behind them (pxs-core's).
# An application without Qt, or one that moved to the core types, includes <pxs/core/tree/parameter.h> and does not need it.
add_library(pxs_params STATIC src/params/Parameter.cpp src/params/pxs/Parameter.h src/params/pxs/ChangeFeed.h)
add_library(pxs::params ALIAS pxs_params)
target_include_directories(pxs_params PUBLIC src/params)
target_link_libraries(pxs_params PUBLIC pxs-core::tree pxs-core::feed pxs::convert Qt6::Core)

# The look: colour tokens, dark palettes, the style sheet every application shares
add_library(pxs_theme STATIC src/theme/Theme.cpp src/theme/pxs/Theme.h)
add_library(pxs::theme ALIAS pxs_theme)
target_include_directories(pxs_theme PUBLIC src/theme)
target_link_libraries(pxs_theme PUBLIC Qt6::Widgets)

add_library(pxs_tree INTERFACE)
add_library(pxs::tree ALIAS pxs_tree)
target_link_libraries(pxs_tree INTERFACE pxs::params pxs::osc)

# OSC and OSCQuery over the network: UDP in and out, the HTTP side, the mDNS announcement. Every application is
# reachable the same way.
add_library(pxs_oscquery STATIC src/oscquery/QueryServer.cpp src/oscquery/MdnsResponder.cpp src/oscquery/OscService.cpp
    src/oscquery/pxs/QueryServer.h src/oscquery/pxs/MdnsResponder.h src/oscquery/pxs/OscService.h)
add_library(pxs::oscquery ALIAS pxs_oscquery)
set_target_properties(pxs_oscquery PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_oscquery PUBLIC src/oscquery)
target_link_libraries(pxs_oscquery PUBLIC pxs::tree Qt6::Network)

# Widgets used by several applications: the slider field (drag, type, snap) and the color picker
add_library(pxs_widgets STATIC src/widgets/Fields.cpp src/widgets/Fields.h src/widgets/SliderField.h
    src/widgets/ColorPicker.cpp src/widgets/ColorPicker.h)
add_library(pxs::widgets ALIAS pxs_widgets)
set_target_properties(pxs_widgets PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_widgets PUBLIC src/widgets)
target_link_libraries(pxs_widgets PUBLIC Qt6::Widgets)

# Interactions: a value driven from outside (a key, a MIDI controller, an OSC message, a signal) through a chain of
# filters (remap, average, attack-release, curve, threshold, toggle, envelope…), with pick-up and an Action mode; the
# graph of them. From NyxMap, where it is the Interaction panel's model. Qt Core only.
add_library(pxs_interaction STATIC src/interaction/Interactions.cpp src/interaction/Anim.cpp src/interaction/TimelineHost.cpp src/interaction/pxs/TimelineHost.h src/interaction/Interactions.h
    src/interaction/InteractionGraph.h src/interaction/Anim.h src/interaction/OnsetAnalysis.h src/interaction/Published.h)
add_library(pxs::interaction ALIAS pxs_interaction)
target_include_directories(pxs_interaction PUBLIC src/interaction)
target_link_libraries(pxs_interaction PUBLIC Qt6::Core pxs-core::timeline pxs-core::filter pxs::params)

# Sequences, the cue lists of a show (from NyxMap): ordered steps that recall a snapshot or a preset or drive a timeline, played by GO,
# with their waits, follows, groups, tracking and clocks. The application answers what a step needs (SequencerHost.h); the musical
# clock (BeatClock.h) and the timings are here too
add_library(pxs_sequence STATIC src/sequence/Sequencer.cpp src/sequence/BeatClock.cpp src/sequence/TimingHistogram.cpp
    src/sequence/pxs/Sequencer.h src/sequence/pxs/SequencerHost.h src/sequence/pxs/BeatClock.h src/sequence/pxs/AnnotatedMutex.h
    src/sequence/pxs/TimingHistogram.h)
add_library(pxs::sequence ALIAS pxs_sequence)
set_target_properties(pxs_sequence PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_sequence PUBLIC src/sequence)
target_link_libraries(pxs_sequence PUBLIC pxs::interaction pxs::params Qt6::Core)

# A sequence as a cue list on screen, for an application whose steps drive timelines (OSCQlock): GO, the step on standby, the times
# of the steps on their way in NyxMap's colours, undo steps. Qt Widgets
add_library(pxs_sequenceview STATIC src/sequenceview/SequenceView.cpp src/sequenceview/pxs/SequenceView.h)
add_library(pxs::sequenceview ALIAS pxs_sequenceview)
set_target_properties(pxs_sequenceview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_sequenceview PUBLIC src/sequenceview)
target_link_libraries(pxs_sequenceview PUBLIC pxs::sequence Qt6::Widgets)

# Expose an interface's own widgets (check box, number field, menu, text field, button) as parameters and messages
add_library(pxs_bind STATIC src/bind/Bind.cpp src/bind/pxs/Bind.h)
add_library(pxs::bind ALIAS pxs_bind)
set_target_properties(pxs_bind PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_bind PUBLIC src/bind)
target_link_libraries(pxs_bind PUBLIC pxs::tree pxs::widgets)

# Controls: the model of a control (kinds, JSON, its place on the page) and the page an application serves for them: a free grid,
# pages for the groups, Edit mode, SVG widgets, and the server (HTTP and WebSocket) that gives a page its controls. Qt Core and Network.
add_library(pxs_controls STATIC src/controls/Server.cpp src/controls/ControlList.cpp src/controls/Filters.cpp src/controls/pxs/Filters.h src/controls/pxs/ControlList.h src/controls/pxs/Server.h src/controls/pxs/Control.h src/controls/pxs/Physics.h src/controls/pxs/ControlsPage.h)
add_library(pxs::controls ALIAS pxs_controls)
set_target_properties(pxs_controls PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_controls PUBLIC src/controls)
target_link_libraries(pxs_controls PUBLIC pxs::params pxs::osc pxs::interaction pxs-core::physics Qt6::Core Qt6::Network)

# DMX over the network: the frames of Art-Net and sACN, encoded and decoded. Qt Core only.
add_library(pxs_dmx STATIC src/dmx/Dmx.cpp src/dmx/pxs/Dmx.h)
add_library(pxs::dmx ALIAS pxs_dmx)
target_include_directories(pxs_dmx PUBLIC src/dmx)
target_link_libraries(pxs_dmx PUBLIC Qt6::Core PRIVATE pxs-core::dmx)

# DMX on the network: a Monitor (Art-Net and sACN in, what each source sends, the merge of the sources) and a Transmitter. From DMXPulse.
add_library(pxs_dmxnet STATIC src/dmx/DmxNet.cpp src/dmx/pxs/DmxNet.h)
add_library(pxs::dmxnet ALIAS pxs_dmxnet)
set_target_properties(pxs_dmxnet PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pxs_dmxnet PUBLIC pxs::dmx Qt6::Network)

# The DMX outputs of an application: settings, routing, and the Sender that streams the universes on a thread of its own. Art-Net and sACN.
add_library(pxs_dmxout STATIC src/dmxout/DmxOut.cpp src/dmxout/pxs/DmxOut.h)
add_library(pxs::dmxout ALIAS pxs_dmxout)
set_target_properties(pxs_dmxout PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_dmxout PUBLIC src/dmxout)
target_link_libraries(pxs_dmxout PUBLIC pxs::dmx Qt6::Network Threads::Threads)

# DMX on screen: the grid of 512 channels with its editor (the network's level above the level to send). From DMXPulse. Qt Widgets.
add_library(pxs_dmxview STATIC src/dmxview/ChannelGrid.cpp src/dmxview/LevelControl.cpp src/dmxview/pxs/ChannelGrid.h src/dmxview/pxs/LevelControl.h)
add_library(pxs::dmxview ALIAS pxs_dmxview)
set_target_properties(pxs_dmxview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_dmxview PUBLIC src/dmxview)
target_link_libraries(pxs_dmxview PUBLIC Qt6::Widgets)

# Curves on screen: the keys of an animation track (click, drag, draw, easings, Bézier) and the ADSR envelope to take in the hand. From NyxMap. Qt Widgets.
add_library(pxs_curveview STATIC src/curveview/CurveLane.cpp src/curveview/pxs/CurveLane.h src/curveview/pxs/AdsrCurve.h)
add_library(pxs::curveview ALIAS pxs_curveview)
set_target_properties(pxs_curveview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_curveview PUBLIC src/curveview)
target_link_libraries(pxs_curveview PUBLIC pxs::interaction Qt6::Widgets)

# The chain of filters of one signal on screen: cards to fold, move by drag and set, with the signal before and after. From MpOsc. Qt Widgets.
add_library(pxs_filterview STATIC src/filterview/FilterEditor.cpp src/filterview/pxs/FilterEditor.h src/filterview/pxs/SignalMeter.h)
add_library(pxs::filterview ALIAS pxs_filterview)
set_target_properties(pxs_filterview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_filterview PUBLIC src/filterview)
target_link_libraries(pxs_filterview PUBLIC pxs::interaction pxs::widgets pxs::curveview Qt6::Widgets)

# The editor of a timeline (from NyxMap): its transport, its ruler and zoom, and a row per track (keys or a wave, colours, events, points,
# filters), on what an application tells it through a TimelineHost. Qt Widgets.
add_library(pxs_timelineview STATIC src/timelineview/TimelineEditor.cpp src/timelineview/pxs/TimelineEditor.h)
add_library(pxs::timelineview ALIAS pxs_timelineview)
set_target_properties(pxs_timelineview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_timelineview PUBLIC src/timelineview)
target_link_libraries(pxs_timelineview PUBLIC pxs::interaction pxs::params pxs::curveview pxs::widgets Qt6::Widgets)

# The panel that edits a list of DMX outputs, the same in every application that sends DMX. Qt Widgets.
add_library(pxs_dmxoutview STATIC src/dmxoutview/OutputsPanel.cpp src/dmxoutview/pxs/OutputsPanel.h)
add_library(pxs::dmxoutview ALIAS pxs_dmxoutview)
set_target_properties(pxs_dmxoutview PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_dmxoutview PUBLIC src/dmxoutview)
target_link_libraries(pxs_dmxoutview PUBLIC pxs::dmxout Qt6::Widgets)

# MIDI: the messages as interaction events (parse, build, learn), the ports and the inputs to listen to. The ports need an RtMidi,
# which pxs does not carry: an application that has one (it may patch or vendor it) names its target before add_subdirectory,
#   set(PXS_RTMIDI_TARGET rtmidi)
# else a system RtMidi (find_package(RtMidi CONFIG)) is used when there is one. Without either, `available()` is false.
if(NOT PXS_RTMIDI_TARGET)
    find_package(RtMidi CONFIG QUIET)
    if(TARGET RtMidi::rtmidi)
        set(PXS_RTMIDI_TARGET RtMidi::rtmidi)
    endif()
endif()
add_library(pxs_midi STATIC src/midi/Midi.cpp src/midi/MidiMessage.cpp src/midi/pxs/Midi.h)
add_library(pxs::midi ALIAS pxs_midi)
set_target_properties(pxs_midi PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_midi PUBLIC src/midi)
target_link_libraries(pxs_midi PUBLIC pxs::interaction Qt6::Core PRIVATE pxs-core::midi)
if(PXS_RTMIDI_TARGET)
    target_link_libraries(pxs_midi PUBLIC ${PXS_RTMIDI_TARGET})
    target_compile_definitions(pxs_midi PUBLIC PXS_HAS_RTMIDI)
endif()

# OSCQuery client: finds the devices of the network (mDNS), reads a device's tree, follows its values (WebSocket LISTEN when Qt WebSockets is
# there, else polled), writes over OSC, and offers the device to the controls as a ParameterSource.
add_library(pxs_oscclient STATIC src/oscclient/RemoteTree.cpp src/oscclient/Discovery.cpp src/oscclient/Client.cpp
    src/oscclient/pxs/RemoteTree.h src/oscclient/pxs/Discovery.h src/oscclient/pxs/Client.h src/oscclient/pxs/RemoteSource.h)
add_library(pxs::oscclient ALIAS pxs_oscclient)
set_target_properties(pxs_oscclient PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(pxs_oscclient PUBLIC src/oscclient)
target_link_libraries(pxs_oscclient PUBLIC pxs::osc pxs::params pxs::controls pxs::oscquery Qt6::Core Qt6::Network)
if(Qt6WebSockets_FOUND)
    target_link_libraries(pxs_oscclient PUBLIC Qt6::WebSockets)
    target_compile_definitions(pxs_oscclient PUBLIC PXS_HAS_WEBSOCKETS)
endif()

if(PXS_BUILD_TESTS)
    enable_testing()
    add_executable(pxs_osc_test test/osc_test.cpp)
    target_link_libraries(pxs_osc_test PRIVATE pxs::osc)
    add_test(NAME osc COMMAND pxs_osc_test)
    add_executable(pxs_filters_test test/filters_test.cpp)
    target_link_libraries(pxs_filters_test PRIVATE pxs::controls)
    add_test(NAME filters COMMAND pxs_filters_test)
    add_executable(pxs_controls_test test/controls_test.cpp)
    target_link_libraries(pxs_controls_test PRIVATE pxs::controls)
    add_test(NAME controls COMMAND pxs_controls_test)
    add_executable(pxs_control_list_test test/control_list_test.cpp)
    target_link_libraries(pxs_control_list_test PRIVATE pxs::controls)
    add_test(NAME control_list COMMAND pxs_control_list_test)
    add_executable(pxs_dmx_test test/dmx_test.cpp)
    target_link_libraries(pxs_dmx_test PRIVATE pxs::dmx)
    add_test(NAME dmx COMMAND pxs_dmx_test)
    add_executable(pxs_dmxnet_test test/dmxnet_test.cpp)
    target_link_libraries(pxs_dmxnet_test PRIVATE pxs::dmxnet)
    add_test(NAME dmxnet COMMAND pxs_dmxnet_test)
    add_executable(pxs_filterview_test test/filterview_test.cpp)
    target_link_libraries(pxs_filterview_test PRIVATE pxs::filterview Qt6::Test)
    add_test(NAME filterview COMMAND pxs_filterview_test)
    add_executable(pxs_dmxview_test test/dmxview_test.cpp)
    target_link_libraries(pxs_dmxview_test PRIVATE pxs::dmxview Qt6::Test)
    add_test(NAME dmxview COMMAND pxs_dmxview_test)
    add_executable(pxs_dmxout_test test/dmxout_test.cpp)
    target_link_libraries(pxs_dmxout_test PRIVATE pxs::dmxout pxs::dmxnet)
    add_test(NAME dmxout COMMAND pxs_dmxout_test)
    add_executable(pxs_dmxoutview_test test/dmxoutview_test.cpp)
    target_link_libraries(pxs_dmxoutview_test PRIVATE pxs::dmxoutview Qt6::Test)
    add_test(NAME dmxoutview COMMAND pxs_dmxoutview_test)
    set_tests_properties(dmxoutview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(pxs_midi_test test/midi_test.cpp)
    target_link_libraries(pxs_midi_test PRIVATE pxs::midi)
    add_test(NAME midi COMMAND pxs_midi_test)
    add_executable(pxs_oscclient_test test/oscclient_test.cpp)
    target_link_libraries(pxs_oscclient_test PRIVATE pxs::oscclient pxs::tree)
    add_test(NAME oscclient COMMAND pxs_oscclient_test)
    add_executable(pxs_controls_server_test test/server_test.cpp)
    target_link_libraries(pxs_controls_server_test PRIVATE pxs::controls)
    add_test(NAME controls_server COMMAND pxs_controls_server_test)
    add_executable(pxs_params_test test/params_test.cpp)
    target_link_libraries(pxs_params_test PRIVATE pxs::params)
    add_test(NAME params COMMAND pxs_params_test)
    add_executable(pxs_convert_test test/convert_test.cpp)
    target_link_libraries(pxs_convert_test PRIVATE pxs::convert)
    add_test(NAME convert COMMAND pxs_convert_test)
    add_executable(pxs_oscquery_test test/oscquery_test.cpp)
    target_link_libraries(pxs_oscquery_test PRIVATE pxs::oscquery)
    add_test(NAME oscquery COMMAND pxs_oscquery_test)
    add_executable(pxs_oscquery_spec_test test/oscquery_spec_test.cpp)
    target_link_libraries(pxs_oscquery_spec_test PRIVATE pxs::oscquery)
    add_test(NAME oscquery_spec COMMAND pxs_oscquery_spec_test)
    if(Qt6WebSockets_FOUND)
        add_executable(pxs_websocket_test test/websocket_test.cpp)
        target_link_libraries(pxs_websocket_test PRIVATE pxs::oscquery Qt6::WebSockets)
        add_test(NAME websocket COMMAND pxs_websocket_test)
    endif()
    add_executable(pxs_bind_test test/bind_test.cpp)
    target_link_libraries(pxs_bind_test PRIVATE pxs::bind)
    add_test(NAME bind COMMAND pxs_bind_test)
    add_executable(pxs_interaction_graph_test test/interaction_graph_test.cpp)
    target_link_libraries(pxs_interaction_graph_test PRIVATE pxs::interaction)
    add_test(NAME interaction_graph COMMAND pxs_interaction_graph_test)
    add_executable(pxs_timelineview_test test/timelineview_test.cpp)
    target_link_libraries(pxs_timelineview_test PRIVATE pxs::timelineview)
    add_test(NAME timelineview COMMAND pxs_timelineview_test)
    set_tests_properties(timelineview PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(pxs_anim_test test/anim_test.cpp)
    target_link_libraries(pxs_anim_test PRIVATE pxs::interaction)
    add_test(NAME anim COMMAND pxs_anim_test)
    add_executable(pxs_sequence_test test/sequence_test.cpp)
    target_link_libraries(pxs_sequence_test PRIVATE pxs::sequence)
    add_test(NAME sequence COMMAND pxs_sequence_test)
    add_executable(pxs_sequenceview_test test/sequenceview_test.cpp)
    target_link_libraries(pxs_sequenceview_test PRIVATE pxs::sequenceview)
    add_test(NAME sequenceview COMMAND pxs_sequenceview_test)
    set_tests_properties(sequenceview PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(pxs_interaction_test test/interaction_test.cpp)
    target_link_libraries(pxs_interaction_test PRIVATE pxs::interaction)
    add_test(NAME interaction COMMAND pxs_interaction_test)
    add_executable(pxs_theme_test test/theme_test.cpp)
    target_link_libraries(pxs_theme_test PRIVATE pxs::theme)
    add_test(NAME theme COMMAND pxs_theme_test)
    add_executable(pxs_widgets_test test/widgets_test.cpp)
    target_link_libraries(pxs_widgets_test PRIVATE pxs::widgets)
    add_test(NAME widgets COMMAND pxs_widgets_test)
    add_test(NAME build_id COMMAND ${CMAKE_COMMAND} -DPXS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test/build_id_test.cmake)
endif()
