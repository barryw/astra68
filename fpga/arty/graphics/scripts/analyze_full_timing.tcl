# Classify every failing setup endpoint in a routed Arty graphics checkpoint.
#
# Required environment:
#   ASTRA_DCP     routed design checkpoint
#   ASTRA_REPORT  destination TSV report
# Optional:
#   ASTRA_MAX_PATHS  maximum paths to inspect (default 20000)
#   ASTRA_DETAIL_REPORT  destination detailed timing report

foreach required {ASTRA_DCP ASTRA_REPORT} {
    if {![info exists ::env($required)] || $::env($required) eq ""} {
        puts stderr "missing required environment variable $required"
        exit 2
    }
}

set max_paths 20000
if {[info exists ::env(ASTRA_MAX_PATHS)] &&
    $::env(ASTRA_MAX_PATHS) ne ""} {
    set max_paths $::env(ASTRA_MAX_PATHS)
}

open_checkpoint $::env(ASTRA_DCP)
set paths [get_timing_paths -quiet -delay_type max \
    -slack_lesser_than 0.0 -max_paths $max_paths -nworst 1]

if {[info exists ::env(ASTRA_DETAIL_REPORT)] &&
    $::env(ASTRA_DETAIL_REPORT) ne ""} {
    report_timing -delay_type max -slack_lesser_than 0.0 \
        -max_paths $max_paths -nworst 1 -input_pins -nets \
        -file $::env(ASTRA_DETAIL_REPORT)
}

array set category_count {}
array set category_worst {}

set report [open $::env(ASTRA_REPORT) w]
puts $report "slack_ns\tdatapath_ns\tlogic_levels\tcategory\tsource\tdestination"

foreach path $paths {
    set source [get_property NAME [get_property STARTPOINT_PIN $path]]
    set destination [get_property NAME [get_property ENDPOINT_PIN $path]]
    set slack [get_property SLACK $path]
    set datapath [get_property DATAPATH_DELAY $path]
    set levels [get_property LOGIC_LEVELS $path]

    if {[string match "*sprite_scene_i/descriptor_*" $source] &&
        [string match "*sprite_builder_i/admission_*" $destination]} {
        set category descriptor_geometry
    } elseif {[string match "*sprite_scene_i/active_palette*" $source] &&
              [string match "*sprite_builder_i/collision_qualify_alpha*" \
                  $destination]} {
        set category palette_collision_dsp
    } elseif {[string match "*sprite_builder_i/blend*" $source] ||
              [string match "*sprite_builder_i/blend*" $destination]} {
        set category sprite_blend
    } elseif {[string match "*sprite_builder_i/row_stage*" $source] &&
              [string match "*sprite_scene_i/active_palette*" $destination]} {
        set category sprite_palette_address
    } elseif {[string match "*sprite*" $source] ||
              [string match "*sprite*" $destination]} {
        set category sprite_other
    } else {
        set category non_sprite
    }

    incr category_count($category)
    if {![info exists category_worst($category)] ||
        $slack < $category_worst($category)} {
        set category_worst($category) $slack
    }

    puts $report [join [list $slack $datapath $levels $category \
        $source $destination] "\t"]
}

close $report
puts "ASTRA_TIMING failing_paths=[llength $paths] max_paths=$max_paths"
foreach category [lsort [array names category_count]] {
    puts "ASTRA_TIMING category=$category count=$category_count($category) worst_ns=$category_worst($category)"
}
puts "ASTRA_TIMING report=$::env(ASTRA_REPORT)"
exit 0
