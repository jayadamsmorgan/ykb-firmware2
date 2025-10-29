WB = west build app --pristine --sysbuild
MENU = -t menuconfig

EXTRA_CONF = -DEXTRA_CONF_FILE=

DBG_CONF = conf/debug.conf
LEFT_CONF = conf/left.conf
RIGHT_CONF = conf/right.conf
DBG_LEFT_CONF = "$(DBG_CONF);$(LEFT_CONF)"
DBG_RIGHT_CONF = "$(DBG_CONF);$(RIGHT_CONF)"

LEFT = $(EXTRA_CONF)$(LEFT_CONF)
RIGHT = $(EXTRA_CONF)$(RIGHT_CONF)
LEFT_DBG = $(EXTRA_CONF)$(DBG_LEFT_CONF)
RIGHT_DBG = $(EXTRA_CONF)$(DBG_RIGHT_CONF)

DACTYL_V1 = -b dactyl_v1
CHOCO_V1 = -b choco_v1

dactyl_v1_left:
	$(WB) $(DACTYL_V1) -- $(LEFT)

dactyl_v1_right:
	$(WB) $(DACTYL_V1) -- $(RIGHT)

dactyl_v1_left_menu:
	$(WB) $(DACTYL_V1) $(MENU) -- $(LEFT)

dactyl_v1_right_menu:
	$(WB) $(DACTYL_V1) $(MENU) -- $(RIGHT)

dactyl_v1_left_dbg:
	$(WB) $(DACTYL_V1) -- $(LEFT_DBG)

dactyl_v1_right_dbg:
	$(WB) $(DACTYL_V1) -- $(RIGHT_DBG)

dactyl_v1_left_dbg_menu:
	$(WB) $(DACTYL_V1) $(MENU) -- $(LEFT_DBG)

dactyl_v1_right_dbg_menu:
	$(WB) $(DACTYL_V1) $(MENU) -- $(RIGHT_DBG)

choco_v1_left:
	$(WB) $(CHOCO_V1) -- $(LEFT)

choco_v1_right:
	$(WB) $(CHOCO_V1) -- $(RIGHT)

choco_v1_left_menu:
	$(WB) $(CHOCO_V1) $(MENU) -- $(LEFT)

choco_v1_right_menu:
	$(WB) $(CHOCO_V1) $(MENU) -- $(RIGHT)

choco_v1_left_dbg:
	$(WB) $(CHOCO_V1) -- $(LEFT_DBG)

choco_v1_right_dbg:
	$(WB) $(CHOCO_V1) -- $(RIGHT_DBG)

choco_v1_left_dbg_menu:
	$(WB) $(CHOCO_V1) $(MENU) -- $(LEFT_DBG)

choco_v1_right_dbg_menu:
	$(WB) $(CHOCO_V1) $(MENU) -- $(RIGHT_DBG)

clean:
	rm -rf build
