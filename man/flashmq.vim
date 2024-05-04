" Vim syntax file
" Language:             FlashMQ configuration file

"
" Goals:
"
" Make incorrect / unknown options stand out. Special attention is given to
" also have this work correctly for blocks, E.G., the toplevel directive
" `log_file` is colored in a global scope but not when it's used inside a
" `listen` block
"
" https://vimdoc.sourceforge.net/htmldoc/syntax.html
"
" TODO:
" - Test number of arguments, specifically: most options take 1 argument, but
"   fe. bridge__subscribe takes an optional second argument
" - Deal with quoted options?
"

if exists("b:current_syntax")
  finish
endif
let b:current_syntax = "flashmq"

syn region  fmqComment               display oneline start='^\s*#' end='$'

" We "render" fmqWrongBlock as Error (which makes it red), but then also use
" transparent which makes it be the color of the parent, so it all becomes a
" neutral color.
" Without the transparent+Error trick you would see fmqTopLevelDirective
" highlighted inside blocks which is undesirable: you can't specify `log_file`
" inside a `listen` block, so it shouldn't get colorized.
" 
" Real blocks (like `listen` and `bridge`) are defined later and thus get a
" higher priority, and will replace this match.
syn region fmqWrongBlock  start=+^.*{+ end=+}+ transparent contains=NONE,fmqComment

hi link fmqComment              Comment
hi link fmqWrongBlock           Error
hi link fmqTopLevelDirective    Type

" The rest of this file has been dynamically generated
"
" Local scopes
"

syn match fmqTopLevelDirective "^\s*allow_anonymous\s"
syn match fmqTopLevelDirective "^\s*allow_anonymous$"
syn match fmqTopLevelDirective "^\s*allow_unsafe_clientid_chars\s"
syn match fmqTopLevelDirective "^\s*allow_unsafe_clientid_chars$"
syn match fmqTopLevelDirective "^\s*allow_unsafe_username_chars\s"
syn match fmqTopLevelDirective "^\s*allow_unsafe_username_chars$"
syn match fmq_bridge_Directive "^\s*address\s" contained
syn match fmq_bridge_Directive "^\s*address$" contained
syn match fmq_bridge_Directive "^\s*bridge_protocol_bit\s" contained
syn match fmq_bridge_Directive "^\s*bridge_protocol_bit$" contained
syn match fmq_bridge_Directive "^\s*ca_dir\s" contained
syn match fmq_bridge_Directive "^\s*ca_dir$" contained
syn match fmq_bridge_Directive "^\s*ca_file\s" contained
syn match fmq_bridge_Directive "^\s*ca_file$" contained
syn match fmq_bridge_Directive "^\s*clientid_prefix\s" contained
syn match fmq_bridge_Directive "^\s*clientid_prefix$" contained
syn match fmq_bridge_Directive "^\s*inet_protocol\s" contained
syn match fmq_bridge_Directive "^\s*inet_protocol$" contained
syn match fmq_bridge_Directive "^\s*keepalive\s" contained
syn match fmq_bridge_Directive "^\s*keepalive$" contained
syn match fmq_bridge_Directive "^\s*local_clean_start\s" contained
syn match fmq_bridge_Directive "^\s*local_clean_start$" contained
syn match fmq_bridge_Directive "^\s*local_session_expiry_interval\s" contained
syn match fmq_bridge_Directive "^\s*local_session_expiry_interval$" contained
syn match fmq_bridge_Directive "^\s*local_username\s" contained
syn match fmq_bridge_Directive "^\s*local_username$" contained
syn match fmq_bridge_Directive "^\s*max_incoming_topic_aliases\s" contained
syn match fmq_bridge_Directive "^\s*max_incoming_topic_aliases$" contained
syn match fmq_bridge_Directive "^\s*max_outgoing_topic_aliases\s" contained
syn match fmq_bridge_Directive "^\s*max_outgoing_topic_aliases$" contained
syn match fmq_bridge_Directive "^\s*port\s" contained
syn match fmq_bridge_Directive "^\s*port$" contained
syn match fmq_bridge_Directive "^\s*protocol_version\s" contained
syn match fmq_bridge_Directive "^\s*protocol_version$" contained
syn match fmq_bridge_Directive "^\s*publish\s" contained
syn match fmq_bridge_Directive "^\s*publish$" contained
syn match fmq_bridge_Directive "^\s*remote_clean_start\s" contained
syn match fmq_bridge_Directive "^\s*remote_clean_start$" contained
syn match fmq_bridge_Directive "^\s*remote_password\s" contained
syn match fmq_bridge_Directive "^\s*remote_password$" contained
syn match fmq_bridge_Directive "^\s*remote_retain_available\s" contained
syn match fmq_bridge_Directive "^\s*remote_retain_available$" contained
syn match fmq_bridge_Directive "^\s*remote_session_expiry_interval\s" contained
syn match fmq_bridge_Directive "^\s*remote_session_expiry_interval$" contained
syn match fmq_bridge_Directive "^\s*remote_username\s" contained
syn match fmq_bridge_Directive "^\s*remote_username$" contained
syn match fmq_bridge_Directive "^\s*subscribe\s" contained
syn match fmq_bridge_Directive "^\s*subscribe$" contained
syn match fmq_bridge_Directive "^\s*tcp_nodelay\s" contained
syn match fmq_bridge_Directive "^\s*tcp_nodelay$" contained
syn match fmq_bridge_Directive "^\s*tls\s" contained
syn match fmq_bridge_Directive "^\s*tls$" contained
syn match fmq_bridge_Directive "^\s*use_saved_clientid\s" contained
syn match fmq_bridge_Directive "^\s*use_saved_clientid$" contained
syn match fmqTopLevelDirective "^\s*client_initial_buffer_size\s"
syn match fmqTopLevelDirective "^\s*client_initial_buffer_size$"
syn match fmqTopLevelDirective "^\s*client_max_write_buffer_size\s"
syn match fmqTopLevelDirective "^\s*client_max_write_buffer_size$"
syn match fmqTopLevelDirective "^\s*expire_retained_messages_after_seconds\s"
syn match fmqTopLevelDirective "^\s*expire_retained_messages_after_seconds$"
syn match fmqTopLevelDirective "^\s*expire_sessions_after_seconds\s"
syn match fmqTopLevelDirective "^\s*expire_sessions_after_seconds$"
syn match fmqTopLevelDirective "^\s*include_dir\s"
syn match fmqTopLevelDirective "^\s*include_dir$"
syn match fmq_listen_Directive "^\s*allow_anonymous\s" contained
syn match fmq_listen_Directive "^\s*allow_anonymous$" contained
syn match fmq_listen_Directive "^\s*client_verification_ca_dir\s" contained
syn match fmq_listen_Directive "^\s*client_verification_ca_dir$" contained
syn match fmq_listen_Directive "^\s*client_verification_ca_file\s" contained
syn match fmq_listen_Directive "^\s*client_verification_ca_file$" contained
syn match fmq_listen_Directive "^\s*client_verification_still_do_authn\s" contained
syn match fmq_listen_Directive "^\s*client_verification_still_do_authn$" contained
syn match fmq_listen_Directive "^\s*fullchain\s" contained
syn match fmq_listen_Directive "^\s*fullchain$" contained
syn match fmq_listen_Directive "^\s*haproxy\s" contained
syn match fmq_listen_Directive "^\s*haproxy$" contained
syn match fmq_listen_Directive "^\s*inet4_bind_address\s" contained
syn match fmq_listen_Directive "^\s*inet4_bind_address$" contained
syn match fmq_listen_Directive "^\s*inet6_bind_address\s" contained
syn match fmq_listen_Directive "^\s*inet6_bind_address$" contained
syn match fmq_listen_Directive "^\s*inet_protocol\s" contained
syn match fmq_listen_Directive "^\s*inet_protocol$" contained
syn match fmq_listen_Directive "^\s*port\s" contained
syn match fmq_listen_Directive "^\s*port$" contained
syn match fmq_listen_Directive "^\s*privkey\s" contained
syn match fmq_listen_Directive "^\s*privkey$" contained
syn match fmq_listen_Directive "^\s*protocol\s" contained
syn match fmq_listen_Directive "^\s*protocol$" contained
syn match fmq_listen_Directive "^\s*tcp_nodelay\s" contained
syn match fmq_listen_Directive "^\s*tcp_nodelay$" contained
syn match fmqTopLevelDirective "^\s*log_debug\s"
syn match fmqTopLevelDirective "^\s*log_debug$"
syn match fmqTopLevelDirective "^\s*log_file\s"
syn match fmqTopLevelDirective "^\s*log_file$"
syn match fmqTopLevelDirective "^\s*log_level\s"
syn match fmqTopLevelDirective "^\s*log_level$"
syn match fmqTopLevelDirective "^\s*log_subscriptions\s"
syn match fmqTopLevelDirective "^\s*log_subscriptions$"
syn match fmqTopLevelDirective "^\s*max_event_loop_drift\s"
syn match fmqTopLevelDirective "^\s*max_event_loop_drift$"
syn match fmqTopLevelDirective "^\s*max_incoming_topic_alias_value\s"
syn match fmqTopLevelDirective "^\s*max_incoming_topic_alias_value$"
syn match fmqTopLevelDirective "^\s*max_outgoing_topic_alias_value\s"
syn match fmqTopLevelDirective "^\s*max_outgoing_topic_alias_value$"
syn match fmqTopLevelDirective "^\s*max_packet_size\s"
syn match fmqTopLevelDirective "^\s*max_packet_size$"
syn match fmqTopLevelDirective "^\s*minimum_wildcard_subscription_depth\s"
syn match fmqTopLevelDirective "^\s*minimum_wildcard_subscription_depth$"
syn match fmqTopLevelDirective "^\s*mosquitto_acl_file\s"
syn match fmqTopLevelDirective "^\s*mosquitto_acl_file$"
syn match fmqTopLevelDirective "^\s*mosquitto_password_file\s"
syn match fmqTopLevelDirective "^\s*mosquitto_password_file$"
syn match fmqTopLevelDirective "^\s*overload_mode\s"
syn match fmqTopLevelDirective "^\s*overload_mode$"
syn match fmqTopLevelDirective "^\s*plugin\s"
syn match fmqTopLevelDirective "^\s*plugin$"
syn match fmqTopLevelDirective "^\s*plugin_opt_\s"
syn match fmqTopLevelDirective "^\s*plugin_opt_$"
syn match fmqTopLevelDirective "^\s*plugin_serialize_auth_checks\s"
syn match fmqTopLevelDirective "^\s*plugin_serialize_auth_checks$"
syn match fmqTopLevelDirective "^\s*plugin_serialize_init\s"
syn match fmqTopLevelDirective "^\s*plugin_serialize_init$"
syn match fmqTopLevelDirective "^\s*plugin_timer_period\s"
syn match fmqTopLevelDirective "^\s*plugin_timer_period$"
syn match fmqTopLevelDirective "^\s*quiet\s"
syn match fmqTopLevelDirective "^\s*quiet$"
syn match fmqTopLevelDirective "^\s*retained_messages_delivery_limit\s"
syn match fmqTopLevelDirective "^\s*retained_messages_delivery_limit$"
syn match fmqTopLevelDirective "^\s*retained_messages_mode\s"
syn match fmqTopLevelDirective "^\s*retained_messages_mode$"
syn match fmqTopLevelDirective "^\s*retained_messages_node_limit\s"
syn match fmqTopLevelDirective "^\s*retained_messages_node_limit$"
syn match fmqTopLevelDirective "^\s*rlimit_nofile\s"
syn match fmqTopLevelDirective "^\s*rlimit_nofile$"
syn match fmqTopLevelDirective "^\s*shared_subscription_targeting\s"
syn match fmqTopLevelDirective "^\s*shared_subscription_targeting$"
syn match fmqTopLevelDirective "^\s*storage_dir\s"
syn match fmqTopLevelDirective "^\s*storage_dir$"
syn match fmqTopLevelDirective "^\s*thread_count\s"
syn match fmqTopLevelDirective "^\s*thread_count$"
syn match fmqTopLevelDirective "^\s*websocket_set_real_ip_from\s"
syn match fmqTopLevelDirective "^\s*websocket_set_real_ip_from$"
syn match fmqTopLevelDirective "^\s*wildcard_subscription_deny_mode\s"
syn match fmqTopLevelDirective "^\s*wildcard_subscription_deny_mode$"
syn match fmqTopLevelDirective "^\s*wills_enabled\s"
syn match fmqTopLevelDirective "^\s*wills_enabled$"
syn match fmqTopLevelDirective "^\s*zero_byte_username_is_anonymous\s"
syn match fmqTopLevelDirective "^\s*zero_byte_username_is_anonymous$"

"
" Global scopes
"

syn region fmq_bridge_InnerBlock start="{" end="}" contains=fmq_bridge_Directive,fmqComment
syn region fmq_bridge_Block start="^bridge\s" end="$" contains=fmq_bridge_InnerBlock
syn region fmq_bridge_Block start="^bridge$" end="$"
hi link fmq_bridge_Directive Type
hi link fmq_bridge_Block Statement

syn region fmq_listen_InnerBlock start="{" end="}" contains=fmq_listen_Directive,fmqComment
syn region fmq_listen_Block start="^listen\s" end="$" contains=fmq_listen_InnerBlock
syn region fmq_listen_Block start="^listen$" end="$"
hi link fmq_listen_Directive Type
hi link fmq_listen_Block Statement

