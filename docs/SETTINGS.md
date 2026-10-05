# All settings

Type these in the console (`~`). The values shown are the defaults that `install.bat` applies (`config/fpbody.cfg`). Changes are saved automatically to `cryoffear\fpbody.cfg`.

## Body
| command | default | effect |
|---|---|---|
| `cl_fpbody` | 1 | body on/off |
| `cl_fpbody_offset` | 15 | how far the body sits behind the camera (standing) |
| `cl_fpbody_crouchoffset` | 20 | the same, crouched |
| `cl_fpbody_zoffset` | 0 | raise/lower the body |
| `cl_fpbody_arms` | 0 | 1 = also draw the body's arms |
| `cl_fpbody_simon` | models/cutscene/player.mdl | model used for the body |
| `cl_fpbody_debug` | 0 | print body state once a second |
| `fpbody_info` / `fpbody_reload` | | print state / rebuild the body model |

## Camera
| command | default | effect |
|---|---|---|
| `cl_fpcam` | 1 | all camera effects |
| `cl_fpcam_bob` | 1 | head bob strength |
| `cl_fpcam_impact` | 1 | footstep and landing kick |
| `cl_fpcam_lag` | 1 | view lag on fast turns (max 2.5°) |
| `cl_fpcam_tilt` | 1 | lean into strafes and turns |
| `cl_fpcam_pitchmax` | 80 | look up/down limit (degrees) |
| `cl_fpcam_maxroll` | 5 | horizon lock: maximum roll (degrees) |

## Hands and weapon
| command | default | effect |
|---|---|---|
| `cl_fpvm` | 1 | all hand/weapon effects |
| `cl_fpvm_sway` | 1 | inertia sway |
| `cl_fpvm_breath` | 1 | idle breathing |
| `cl_fpvm_action` | 1 | sprint/jump/land/fire/reload offsets |
| `cl_fists` | 0 | fists mode: the nightstick becomes bare fists (mouse 1 jab, mouse 2 cross). `fp_fists` toggles it |

## Field of view
| command | default | effect |
|---|---|---|
| `cl_fpfov` | 1 | FOV shifts on/off |
| `cl_fpfov_sprint` | 6 | % wider while sprinting |
| `cl_fpfov_ads` | 8 | % narrower while aiming |

## Graphics
| command | default | effect |
|---|---|---|
| `cl_pp` | 1 | all graphics effects |
| `cl_pp_ssao` | 0.6 | ambient occlusion (max 1.5) |
| `cl_pp_ssao_radius` | 28 | AO reach (world units) |
| `cl_pp_contact` | 0.4 | contact shadows under objects (max 1.5) |
| `cl_pp_contact_len` | 14 | contact shadow reach |
| `cl_pp_gi` | 0.6 | bounce light |
| `cl_pp_gi_radius` | 64 | bounce reach |
| `cl_pp_volumetric` | 1.5 | flashlight beam in the air |
| `cl_pp_volumetric_always` | 0 | 1 = beam even with the light off |
| `cl_pp_flashshadows` | 0.8 | shadows cast by the flashlight (railings, boxes, enemies) |
| `cl_pp_dust` | 1 | dust specks floating in the flashlight beam |
| `cl_pp_aniso` | 16 | sharper floors and distant surfaces (anisotropic filtering, 0 = off; applied on map load) |
| `cl_pp_shafts` | 1 | light shafts |
| `cl_pp_fog` | 0.4 | distance fog |
| `cl_pp_fog_color` | "0.07 0.075 0.08" | fog colour (RGB 0-1) |
| `cl_pp_bloom` | 1 | glow |
| `cl_pp_bloom_threshold` | 0.2 | how bright before glowing |
| `cl_pp_lensdirt` | 0.3 | lens smudges lit by glow |
| `cl_pp_motionblur` | 1 | motion blur |
| `cl_pp_dof` | 1 | background blur while aiming |
| `cl_pp_dof_far` | 0 | blur on distant scenery |
| `cl_pp_aa` | 1 | FXAA anti-aliasing |
| `cl_pp_sharpen` | 1 | sharpening |
| `cl_pp_exposure` | 0.8 | brightness |
| `cl_pp_adapt` | 1 | eye adaptation |
| `cl_pp_tonemap` | 1 | soft highlights |
| `cl_pp_saturation` | 0.85 | colour saturation |
| `cl_pp_contrast` | 1.05 | contrast |
| `cl_pp_tint` | "0.94 1.0 1.08" | shadow tint (RGB) |
| `cl_pp_vignette` | 0.25 | dark screen edges |
| `cl_pp_grain` | 0.04 | film grain |
| `cl_pp_ca` | 0.4 | chromatic aberration |
| `cl_pp_hurt` | 1 | damage / low-health screen effect |
| `cl_pp_ssr` | 0 | wet floor reflections (smear on CoF maps) |
| `cl_pp_ssr_puddles` | 1 | reflections in puddle patches only |
| `cl_pp_stats` | 1 | log fps and effect cost to `fpbody.log` every 10 s |
| `cl_pp_debug` | 0 | 1 AO, 2 depth, 3 motion, 4 DoF, 5 bloom+shafts, 6 bounce, 7 beam, 8 reflections, 9 contact, 10 flashlight shadows |

## Projected flashlight (for other GoldSrc games; off in Cry of Fear)
| command | default | effect |
|---|---|---|
| `cl_fplight` | 0 | replace Half-Life's flashlight blob with a projected spotlight |
| `cl_fplight_brightness` | 1 | brightness |
| `cl_fplight_range` | 900 | reach |
| `cl_fplight_fov` | 50 | beam width (degrees); also used by the volumetric beam |
| `cl_fplight_sway` | 1 | how much the light lags the aim |
| `cl_fplight_color` | "1 0.94 0.82" | colour |
| `cl_fplight_models` | 1 | light monsters too |
