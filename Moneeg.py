import xml.etree.ElementTree as ET
from xml.dom import minidom

def create_bpmn_xml(file_name="eeg_acquisition_framework.bpmn"):
    # Define BPMN 2.0 namespaces
    ns_bpmn = "http://www.omg.org/spec/BPMN/20100524/MODEL"
    ET.register_namespace('', ns_bpmn)
    
    # Root element
    definitions = ET.Element(f"{{{ns_bpmn}}}definitions", 
                             id="Definitions_1", 
                             targetNamespace="http://bpmn.io/schema/bpmn")
    
    # Process element
    process = ET.SubElement(definitions, f"{{{ns_bpmn}}}process", 
                            id="Process_EEGAcquisition", 
                            isExecutable="false")
    
    # Define Lanes (Architectural Layers)
    lane_set = ET.SubElement(process, f"{{{ns_bpmn}}}laneSet", id="LaneSet_1")
    
    lanes = {
        "Lane_Physical": ("Physical & Analog Layer", ["StartEvent_1", "Task_Impedance", "Task_SNR", "Task_SPI"]),
        "Lane_Hardware": ("Deterministic Hardware Layer", ["Task_Interrupt", "Task_Sync"]),
        "Lane_Processing": ("Compute & Processing Layer", ["Task_DSP", "Task_Stream", "EndEvent_1"])
    }
    
    for lane_id, (lane_name, flow_nodes) in lanes.items():
        lane = ET.SubElement(lane_set, f"{{{ns_bpmn}}}lane", id=lane_id, name=lane_name)
        for node in flow_nodes:
            ET.SubElement(lane, f"{{{ns_bpmn}}}flowNodeRef").text = node

    # Define Nodes (Tasks and Events)
    nodes = [
        (f"{{{ns_bpmn}}}startEvent", "StartEvent_1", "Session Start"),
        (f"{{{ns_bpmn}}}task", "Task_Impedance", "Execute AC Impedance Measurement"),
        (f"{{{ns_bpmn}}}task", "Task_SNR", "Mitigate Hardware SNR Degradation (Active DRL)"),
        (f"{{{ns_bpmn}}}task", "Task_SPI", "Route Data via Independent SPI Buses"),
        (f"{{{ns_bpmn}}}task", "Task_Interrupt", "Trigger Hardware-Level ISR (Software/USB triggered)"),
        (f"{{{ns_bpmn}}}task", "Task_Sync", "Inject Event Marker for Temporal Synchronization"),
        (f"{{{ns_bpmn}}}task", "Task_DSP", "Execute DSP (IIR Filtering & Referencing)"),
        (f"{{{ns_bpmn}}}task", "Task_Stream", "Format to XDF & Stream Data"),
        (f"{{{ns_bpmn}}}endEvent", "EndEvent_1", "Session End")
    ]
    
    for tag, node_id, name in nodes:
        ET.SubElement(process, tag, id=node_id, name=name)

    # Define Sequence Flows
    sequence_flows = [
        ("Flow_1", "StartEvent_1", "Task_Impedance"),
        ("Flow_2", "Task_Impedance", "Task_SNR"),
        ("Flow_3", "Task_SNR", "Task_SPI"),
        ("Flow_4", "Task_SPI", "Task_Interrupt"),
        ("Flow_5", "Task_Interrupt", "Task_Sync"),
        ("Flow_6", "Task_Sync", "Task_DSP"),
        ("Flow_7", "Task_DSP", "Task_Stream"),
        ("Flow_8", "Task_Stream", "EndEvent_1")
    ]
    
    for flow_id, source, target in sequence_flows:
        ET.SubElement(process, f"{{{ns_bpmn}}}sequenceFlow", 
                      id=flow_id, 
                      sourceRef=source, 
                      targetRef=target)

    # Convert to formatted XML string
    rough_string = ET.tostring(definitions, 'utf-8')
    reparsed = minidom.parseString(rough_string)
    pretty_xml = reparsed.toprettyxml(indent="  ")

    # Write to file
    with open(file_name, "w", encoding="utf-8") as f:
        f.write(pretty_xml)
        
    print(f"BPMN file generated successfully: {file_name}")
    print("Note: Import this file into a tool like Camunda Modeler or bpmn.io to auto-layout the visual diagram.")

if __name__ == "__main__":
    create_bpmn_xml()