#pragma once
// Pack difficile du Temple de la Foret, genere depuis le scan de la salle (locations.txt).
// Chaque entree : stage, salle, objet de stage, parametres, position x y z, angle, vie imposee.
struct PackEntry { const char* stage; int room; const char* obj; unsigned params; float x, y, z; int yaw; int hp; };
static const PackEntry kForestPack[] = {
    {"D_MN05", 0, "E_yk", 0xFFFFFFFFu, 1226.0f, 2950.0f, 6238.0f, 0, 10},
    {"D_MN05", 0, "E_yk", 0xFFFFFFFFu, 250.0f, 4530.0f, 7800.0f, 0, 10},
    {"D_MN05", 0, "E_yk", 0xFFFFFFFFu, -800.0f, 2950.0f, 6280.0f, 0, 10},
    {"D_MN05", 0, "E_yk", 0xFFFFFFFFu, -242.0f, 4630.0f, 7446.0f, 0, 20},
    {"D_MN05", 3, "E_yk", 0xFFFFFFFFu, -5886.0f, 4484.0f, 7953.0f, 0, 10},
    {"D_MN05", 3, "E_yk", 0xFFFFFFFFu, -5168.0f, 3833.0f, 6005.0f, 0, 10},
    {"D_MN05", 3, "E_yk", 0xFFFFFFFFu, -3495.0f, 3200.0f, 8005.0f, 0, 10},
    {"D_MN05", 3, "E_yk", 0xFFFFFFFFu, -4756.0f, 4700.0f, 7723.0f, 0, 20},
    {"D_MN05", 3, "E_yk", 0xFFFFFFFFu, -4356.0f, 4700.0f, 7323.0f, 0, 20},
    {"D_MN05", 4, "E_yk", 0xFFFFFFFFu, 7264.0f, 4344.0f, 1120.0f, 0, 10},
    {"D_MN05", 4, "E_yk", 0xFFFFFFFFu, 7590.0f, 4345.0f, -206.0f, 0, 10},
    {"D_MN05", 4, "E_yk", 0xFFFFFFFFu, -3779.0f, 5615.0f, 2018.0f, 0, 10},
    {"D_MN05", 4, "E_yk", 0xFFFFFFFFu, 317.0f, 5715.0f, 97.0f, 0, 20},
    {"D_MN05", 4, "E_yk", 0xFFFFFFFFu, 717.0f, 5715.0f, -303.0f, 0, 20},
    {"D_MN05B", 51, "E_yk", 0xFFFFFFFFu, 874.0f, 4202.0f, -3710.0f, 0, 20},
    {"D_MN05B", 51, "E_yk", 0xFFFFFFFFu, -926.0f, 4202.0f, -3710.0f, 0, 20},
    {"D_MN05B", 51, "E_yk", 0xFFFFFFFFu, -26.0f, 4202.0f, -2910.0f, 0, 20},
    {"D_MN05", 5, "E_yk", 0xFFFFFFFFu, -14450.0f, 5430.0f, 6300.0f, 0, 10},
    {"D_MN05", 5, "E_yk", 0xFFFFFFFFu, -10550.0f, 3550.0f, 6300.0f, 0, 10},
    {"D_MN05", 5, "E_yk", 0xFFFFFFFFu, -12125.0f, 3650.0f, 6825.0f, 0, 10},
    {"D_MN05", 5, "E_yk", 0xFFFFFFFFu, -11925.0f, 5530.0f, 6139.0f, 0, 20},
    {"D_MN05", 5, "E_yk", 0xFFFFFFFFu, -11525.0f, 5530.0f, 5739.0f, 0, 20},
    {"D_MN05", 7, "E_yk", 0xFFFFFFFFu, -4775.0f, 4599.0f, 13575.0f, 0, 10},
    {"D_MN05", 7, "E_yk", 0xFFFFFFFFu, -6575.0f, 4598.0f, 13275.0f, 0, 10},
    {"D_MN05", 7, "E_yk", 0xFFFFFFFFu, -5925.0f, 4699.0f, 13275.0f, 0, 20},
    {"D_MN05", 1, "E_yk", 0xFFFFFFFFu, 6411.0f, 3502.0f, 7345.0f, 0, 10},
    {"D_MN05", 1, "E_yk", 0xFFFFFFFFu, 6896.0f, 4113.0f, 7760.0f, 0, 10},
    {"D_MN05", 1, "E_yk", 0xFFFFFFFFu, 3450.0f, 3418.0f, 6000.0f, 0, 10},
    {"D_MN05", 1, "E_yk", 0xFFFFFFFFu, 6085.0f, 4257.0f, 6296.0f, 0, 20},
};
static const int kForestPackCount = sizeof(kForestPack) / sizeof(kForestPack[0]);
