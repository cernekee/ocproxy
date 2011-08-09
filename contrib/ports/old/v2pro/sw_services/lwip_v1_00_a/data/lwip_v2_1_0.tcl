###############################################################################
# Copyright (c) 2001-2003 Swedish Institute of Computer Science.
# All rights reserved. 
#
# Copyright (c) 2003 Xilinx, Inc.
# All rights reserved. 
# 
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions 
# are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO 
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS".
# BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS ONE POSSIBLE 
# IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD, XILINX 
# IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE FROM 
# ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING 
# ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.  XILINX 
# EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO THE 
# ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY 
# WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE 
# FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY 
# AND FITNESS FOR A PARTICULAR PURPOSE.
#
# This file is part of the lwIP TCP/IP stack.
#
# Author: Sathya Thammanur <Sathyanarayanan.Thammanur@xilinx.com>
# Author: Chris Borrelli <Chris.Borrelli@xilinx.com>
#
###############################################################################

# Globals
lappend emac_list
lappend gemac_list

###############################################################################
# lwip_drc - run drc checking (future work)
###############################################################################
proc lwip_drc {lib_handle} {
   puts "LWIP DRC..."
   puts "   ...not implemented yet."
}

###############################################################################
# generate - generates the .c file
###############################################################################
proc generate {libname} {
   global emac_list
   global gemac_list

   # generate emac_list of instances to be used with lwip
   set emac_inst_array [xget_handle $libname "ARRAY" "emac_instances"]
   if {[string compare -nocase $emac_inst_array ""] != 0} {
      set emac_inst_array_elems [xget_handle $emac_inst_array "ELEMENTS" "*"]
      puts "XEmac Instances : [llength $emac_inst_array_elems]"
      if {[string compare -nocase $emac_inst_array_elems ""] != 0} {
         set emac_list $emac_inst_array_elems
      }
   }

   # generate gemac_list of instances to be used with lwip
   set gemac_inst_array [xget_handle $libname "ARRAY" "gemac_instances"]
   if {[string compare -nocase $gemac_inst_array ""] != 0} {
      set gemac_inst_array_elems [xget_handle $gemac_inst_array "ELEMENTS" "*"]
      puts "XGEmac Instances : [llength $gemac_inst_array_elems]"
      if {[string compare -nocase $gemac_inst_array_elems ""] != 0} {
         set gemac_list $gemac_inst_array_elems
      }
   }

   # Generate XEmacIf_Config Table in xemacif_g.c file
   if {[string compare -nocase $emac_list ""] != 0} {
      xgen_config_file "xemacif_g.c" "XEmacIf"
   }

   # Generate XGEmacIf_Config Table in xgemacif_g.c file
   if {[string compare -nocase $gemac_list ""] != 0} {
      xgen_config_file "xgemacif_g.c" "XGEmacIf"
   }
}

###############################################################################
# xgen_config_file - does the real work of generating a C header file
###############################################################################
proc xgen_config_file {file_name if_type} {
   global emac_list
   global gemac_list

   lappend list

   puts "Generating $file_name ..."

   set config_file [open [file join "src" $file_name] w]
   xprint_generated_header $config_file "$if_type Configuration"

   puts $config_file "#include \"xparameters.h\""
   set if_file [file join "netif" [format "%s.h" [string tolower $if_type]]]
   puts $config_file "#include \"$if_file\""

   if {[string compare -nocase $if_type "XEmacIf"] == 0} {
      puts $config_file "#include \"xemac.h\""
      set list $emac_list
   } elseif {[string compare -nocase $if_type "XGEmacIf"] == 0} {
      puts $config_file "#include \"xgemac.h\""
      set list $gemac_list
   }
   
   #set list_size [llength $list]
   #puts $config_file "\n/*"
   #puts $config_file " * Number of $if_type Instances"
   #puts $config_file " */\n"
   #puts $config_file [format "#define %s_ConfigTableSize %s" $if_type $list_size]

   puts $config_file "\n/*"
   puts $config_file " * The configuration table for devices"
   puts $config_file " */\n"
   puts $config_file [format "%s_Config %s_ConfigTable\[\] =" $if_type $if_type]
   puts $config_file "\{"

   set start_comma ""

   foreach elem $list {
      puts $config_file [format "%s\t\{" $start_comma]
      set comma ""

      if {[string compare -nocase $if_type "XEmacIf"] == 0} {
         set inst [xget_value $elem "PARAMETER" "emac_instname"]
         set ethaddr1 [xget_value $elem "PARAMETER" "eth_addr1"]
         set ethaddr2 [xget_value $elem "PARAMETER" "eth_addr2"]
         set ethaddr3 [xget_value $elem "PARAMETER" "eth_addr3"]
         set ethaddr4 [xget_value $elem "PARAMETER" "eth_addr4"]
         set ethaddr5 [xget_value $elem "PARAMETER" "eth_addr5"]
         set ethaddr6 [xget_value $elem "PARAMETER" "eth_addr6"]
      } elseif {[string compare -nocase $if_type "XGEmacIf"] == 0} {
         set inst [xget_value $elem "PARAMETER" "emac_instname"]
         set ethaddr1 ""
         set ethaddr2 ""
         set ethaddr3 ""
         set ethaddr4 ""
         set ethaddr5 ""
         set ethaddr6 ""
      }

      # Get the instance handle - 
      # for example, get pointer to an instance named my_opb_ethernet
      # Get the handle to the interrupt port -ip2intc_irpt 
      # (same name in emac and gemac)
      set inst_handle [xget_hwhandle $inst]
      set intr_port_name "IP2INTC_Irpt"

      ########################################################################
      # generate device id
      ########################################################################
      puts -nonewline $config_file [format "%s\t\t%s" $comma [xget_name $inst_handle "DEVICE_ID"]]
      set comma ",\n"

      ########################################################################
      # generate intr id - doesn't work in EDK 3.2.2
      ########################################################################

      # START : CHRISB COMMENTED THIS OUT
      #set intr_id [xget_intr_name $inst_handle $intr_port_name]
      #puts -nonewline $config_file [format "%s\t\t%s" $comma $intr_id]
      # END   : CHRISB COMMENTED THIS OUT

      # START : CHRISB ADDED THIS
      puts -nonewline $config_file [format "%s\t\t%s" $comma "0"]
      # END   : CHRISB ADDED THIS

      ########################################################################
      # generate ethaddr
      ########################################################################
      puts -nonewline $config_file [format "%s\t\t{{%s,%s,%s,%s,%s,%s}}" $comma $ethaddr1 $ethaddr2 $ethaddr3 $ethaddr4 $ethaddr5 $ethaddr6]

      ########################################################################
      # generate instance ptr - always NULL because it is setup at runtime
      ########################################################################
      puts -nonewline $config_file [format "%s\t\tNULL" $comma ]

      puts -nonewline $config_file [format "\n\t\}" ]
      set start_comma ",\n"
   }

   puts $config_file "\n\};"
   close $config_file
}

###############################################################################
# This procedure does not work under EDK 3.2.2
###############################################################################
#proc xget_intr_name {periph_handle port_name} {
#
#    set intr_port [xget_handle $periph_handle "PORT" $port_name]
#    set intr_signal [xget_value $intr_port "VALUE"]
#    #puts "intr_signal : $intr_signal"
#
#    set mhs_handle [xget_handle $periph_handle "parent"]
#    set source_port [xget_connected_ports_handle $mhs_handle $intr_signal "sink"]
#    #puts "sourceport : $source_port"
#
#    set intc_handle [xget_handle $source_port "parent"]
#    set intc_name [xget_value $intc_handle "NAME"]
#
#    set periph_name [xget_value $periph_handle "NAME"]
#
#    set intc_drvhandle [xget_swhandle $intc_name]
#    #puts "intcdrvname: [xget_value $intc_drvhandle "NAME"]"
#
#    set level [xget_value $intc_drvhandle "PARAMETER" "level"]
#    set intr_string ""
#    if {$level == 0} {
#	set intr_string "MASK"
#	set retval [format "XPAR_%s_%s_%s" [string toupper $periph_name] [string toupper $port_name] [string toupper $intr_string]]
#    } elseif {$level == 1} {
#	set intr_string "INTR"
#	set retval [format "XPAR_%s_%s_%s_%s" [string toupper $intc_name] [string toupper $periph_name] [string toupper $port_name] [string toupper $intr_string]]
#    }
#
#    return $retval
#}

###############################################################################
# post_generate - doesn't do anything at the moment
###############################################################################
proc post_generate {libname} {

}

###############################################################################
# execs_generate
# This procedure builds the liblwipv4.a
# library.
###############################################################################
proc execs_generate {libname} {
global errorCode
global errorInfo

   set topdir [xget_value $libname "PARAMETER" "lwip_srcdir"]
   puts "\n********************************"
   puts " Building lwIP library"
   puts "********************************\n"
   puts "Using LWIP sources from directory $topdir "

   if { [catch {exec bash -c "cd src;make all \"TOPDIR=$topdir\" >& logs"} errmsg] } {
      error $errmsg $errorInfo
   }
}
