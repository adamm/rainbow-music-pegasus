include <pcb.scad>;

margin_w = 30;
margin_l = 30;

stencil_w = 210;
stencil_l = 127;
stencil_h = 0.0;

board_w = stencil_w + margin_w;
board_l = stencil_l + margin_l;
board_h = 5.0;

board_x = pcb_w/2 - board_w/2;
board_y = -board_l/2;
board_z = 0;

stencil_x = -margin_w;
stencil_y = -(board_l/2) + margin_l/2;
stencil_z = board_h - stencil_h;

module negative_pcb(x, y, z) {
    translate([x, y, board_h-stencil_h-pcb_h]) //pcb(false);
        linear_extrude(height=board_h+3)
            projection(cut=true)
                pcb(false);
    translate([x, y, -0.01])
        linear_extrude(height=board_h+3)
            offset(delta=-1.75)
                projection(cut=true)
                    pcb(false);
}

module board() {
    difference() {
        translate([board_x, board_y, board_z])
            cube([board_w, board_l, board_h]);
        negative_pcb(0, 2.75, 0);
        mirror([0,1,0])
            negative_pcb(0, 2.75, 0);

        if (stencil_h > 0) {
            // Trim the stencil area
            translate([stencil_x, stencil_y, stencil_z])
                cube([stencil_w, stencil_l, stencil_h+4]);

            // Create a tab for lifting the stencil
            translate([stencil_w/2-pcb_w/2, stencil_y-margin_l, stencil_z])
               cube([stencil_w/4, stencil_l+margin_l*2, stencil_h+4]);
        }
    }
}

board();
