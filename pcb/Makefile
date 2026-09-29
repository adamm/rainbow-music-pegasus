PROJECT=rainbow-music-pegasus
BOM_CSV=../bom.csv
SCH_PDF=../schematic.pdf
PCB_PDF=../pcb.pdf
GERBER_DIR=gerbers
GERBER_JOB=${GERBER_DIR}/${PROJECT}-job.gbrjob
DRILL=${GERBER_DIR}/${PROJECT}-PTH.drl
GERBER_ZIP=${PROJECT}-gerbers.zip

all: ${BOM_CSV} ${SCH_PDF} gerbers #${PCB_PDF}

gerbers: ${GERBER_JOB} ${DRILL}

zip: ${GERBER_ZIP}

.PHONY: all gerbers zip

${BOM_CSV}: ${PROJECT}.kicad_sch
	kicad-cli sch export bom $^ -o $@ --group-by Value,Footprint --fields "Reference,Value,Footprint,Description,Quantity" --ref-delimiter ", "

${SCH_PDF}: ${PROJECT}.kicad_sch
	kicad-cli sch export pdf $^ -o $@

${PCB_PDF}: ${PROJECT}.kicad_pcb
	kicad-cli pcb export pdf $^ -o $@ -l B.Silkscreen,F.Cu,B.Cu,In2.Cu,F.Silkscreen,F.Mask,F.Adhesive,F.Paste,F.Courtyard,Edge.Cuts

# Layers and format come from the board's Plot dialog settings, so GUI and CLI output match
${GERBER_JOB}: ${PROJECT}.kicad_pcb
	rm -f ${GERBER_DIR}/*.gbr ${GERBER_DIR}/*.gbrjob
	kicad-cli pcb export gerbers $^ -o ${GERBER_DIR}/ --board-plot-params --check-zones

# Also writes the NPTH file; slots use routed paths (the GUI default) rather than G85
${DRILL}: ${PROJECT}.kicad_pcb
	rm -f ${GERBER_DIR}/*.drl
	kicad-cli pcb export drill $^ -o ${GERBER_DIR}/ --excellon-separate-th --excellon-oval-format route

${GERBER_ZIP}: ${GERBER_JOB} ${DRILL}
	rm -f $@
	zip -jX $@ ${GERBER_DIR}/*.gbr ${GERBER_DIR}/*.gbrjob ${GERBER_DIR}/*.drl
