sub_inc_song
            lda audio_song
            clc
            adc #1
            cmp #NUM_SONGS
            bcc _inc_song_save
            lda #0
_inc_song_save
            sta audio_song
            rts

sub_dec_song
            lda audio_song
            sec
            sbc #1
            bcs _dec_song_save
            lda #(NUM_SONGS - 1)
_dec_song_save
            sta audio_song
            rts

sub_start_song
            lda audio_song
            lda SONG_TABLE_START_LO,y
            sta audio_song_ptr
            lda SONG_TABLE_START_HI,y
            sta audio_song_ptr + 1
            ldy #0
            sty audio_row_idx
            lda (audio_song_ptr),y
            sta audio_pattern_idx
            iny
            lda (audio_song_ptr),y
            sta audio_pattern_idx+1
            iny
            sty audio_song_order
            rts

sub_play_song
            ldx #NUM_AUDIO_CHANNELS - 1
_audio_loop
            ldy audio_timer,x
            beq _audio_next_note
            dey
            sty audio_timer,x
            jmp _audio_next_channel
_audio_next_note
            ldy audio_pattern_idx,x 
            lda PAT_TABLE_START_LO,y
            sta audio_pattern_ptr
            lda PAT_TABLE_START_HI,y
            sta audio_pattern_ptr + 1
            ldy audio_row_idx
            lda (audio_pattern_ptr),y
            tay                       ; y is now waveform ptr
            lda WF_TABLE_START_LO,y
            sta audio_waveform_ptr
            lda WF_TABLE_START_HI,y
            sta audio_waveform_ptr + 1
            ldy audio_waveform_idx,x
            lda (audio_waveform_ptr),y
            beq _audio_advance_tracker ; check for zero 
            lsr                        ; pull first bit
            bcc _set_registers         ; if set go to load registers
            lsr                        ; check second bit
            bcc _set_cx_vx             ; if clear we are loading aud(c|v)x
            lsr                        ; pull duration bit for later set
            sta audio_fx,x             ; store frequency
            jmp _set_timer_delta       ; jump to duration 
_set_cx_vx  lsr
            bcc _set_vx
            sta audio_cx,x
            jmp _set_timer_delta       ; jump to duration
_set_vx
            sta audio_vx,x
_set_timer_delta
            lda #0
            adc #0                     ; will be 0 or 1 based on carry bit
            sta audio_timer,x
            jmp _audio_advance_waveform
_set_registers
            ; processing all registers
            pha                        ; save timer
            lsr
            lsr
            sta audio_fx,x
            iny
            pla                      
            and #$03
            lsr
            bcs _set_timer_registers
            lda (audio_waveform_ptr),y
            iny
_set_timer_registers
            sta audio_timer,x
            lda (audio_waveform_ptr),y
            lsr
            lsr
            lsr
            lsr
            sta audio_cx,x
            lda (audio_waveform_ptr),y
            and #$0f
            sta audio_vx,x
_audio_advance_waveform
            iny
            sty audio_waveform_idx,x
            jmp _audio_next_channel
_audio_advance_tracker ; got a 0 on waveform
            lda #255
            sta audio_timer,x
            sta audio_waveform_idx,x
_audio_next_channel
            dex
            bpl _audio_loop

            ; update track - check if both waveforms done
            lda audio_waveform_idx
            and audio_waveform_idx+1
            cmp #255
            bne _audio_end            
            lda #0
            sta audio_timer
            sta audio_timer+1
            sta audio_waveform_idx
            sta audio_waveform_idx+1
            ldy audio_row_idx
            iny
            lda (audio_pattern_ptr),y
            cmp #255
            beq _audio_advance_order
            sty audio_row_idx
            jmp sub_play_song; if not 255 loop back 
_audio_advance_order ; got a 255 on pattern
            lda #0
            sta audio_row_idx
            ldy audio_song_order
            lda (audio_song_ptr),y
            cmp #255
            bne _audio_advance_order_advance_pattern
            ldy #0
            lda (audio_song_ptr),y
_audio_advance_order_advance_pattern
            sta audio_pattern_idx
            iny
            lda (audio_song_ptr),y
            sta audio_pattern_idx+1
            iny
            sty audio_song_order
            jmp sub_play_song;  loop back 
_audio_end
            rts

