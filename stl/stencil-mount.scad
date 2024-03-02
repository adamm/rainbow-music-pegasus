include <pcb.scad>;

board_w = 250;
board_h = 3;
board_l = 200;

board_x = pcb_w/2 - board_w/2;
board_y = -board_l/2;
board_z = 0.01;

module negative_pcb(x, y, z) {
    translate([x, y, z+1.4])
        pcb(false);
    translate([x, y, z])
        linear_extrude(height=board_h+2)
            offset(delta=-2.5)
                projection(cut=true)
                    pcb(false);
}

module board() {
    difference() {
        translate([board_x, board_y, board_z])
            cube([board_w, board_l, board_h]);
        negative_pcb(0, 15, 0);
        mirror([0,1,0])
            negative_pcb(0, 15, 0);
    }
}

board();
