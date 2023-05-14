set(component_srcs  "main.c"
                    "../components/ir_protocol/src/ir_builder_rmt_samsung.c"
                    "../components/ir_protocol/src/ir_parser_rmt_samsung.c")

set(component_incs  "."
                    "../components/ir_protocol/include")

idf_component_register(SRCS         "${component_srcs}"
                    INCLUDE_DIRS    "${component_incs}")
