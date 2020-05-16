#include "codec_jpeg_wrapper.h"

struct wrap_jpeg_error_state {
    struct jpeg_error_mgr error_mgr;
    wrap_jpeg_error_handler error_handler;
    void * custom_state;
    jmp_buf error_handler_jmp;
    bool scale_luma_spatially;
    bool gamma_correct_for_srgb_during_spatial_luma_scaling;

};

static void wrap_jpeg_error_exit(j_common_ptr codec_info)
{
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;


    /* Acquire the message. */
    char warning_buffer[JMSG_LENGTH_MAX];
    //Q: If this ever fails to set a null byte we are screwed when we format it later
    codec_info->err->format_message(codec_info, warning_buffer);

    bool result = state->error_handler(state->custom_state, codec_info, &state->error_mgr, state->error_mgr.msg_code, &warning_buffer[0], JMSG_LENGTH_MAX );

    if (result) {
        return;
    }else{
        /* Return control to the setjmp point */
        longjmp(state->error_handler_jmp, 1);
    }
    // Uncomment to permit JPEGs with unknown markers
    // if (state->error_mgr.msg_code == JERR_UNKNOWN_MARKER) return;
    // Destroy memory allocs and temp files
    // Specialized routines are wrappers for jpeg_destroy_compress
    jpeg_destroy(codec_info);
}

//! Ignores warnings
static void flow_jpeg_ignore_message(j_common_ptr codec_info)
{
    // char buffer[JMSG_LENGTH_MAX];
    // codec_info->err->format_message(codec_info, buffer);
    // TODO: maybe create a warnings log in flow_context, and append? Users aren't reading stderr
    // fprintf(stderr, "%s", &buffer[0]);
}
//! Ignores warnings
static void flow_jpeg_ignore_emit(j_common_ptr codec_info, int level)
{
}

size_t wrap_jpeg_error_state_bytes(void){
    return sizeof(struct wrap_jpeg_error_state);
}


void wrap_jpeg_setup_error_handler(j_decompress_ptr codec_info, struct wrap_jpeg_error_state * state, void * custom_state, wrap_jpeg_error_handler  error_handler)
{
    codec_info->err = jpeg_std_error((struct jpeg_error_mgr *) state );
    state->custom_state = custom_state;
    codec_info->err->error_exit = wrap_jpeg_error_exit;
    state->error_mgr.emit_message = flow_jpeg_ignore_emit;
    state->error_mgr.output_message = flow_jpeg_ignore_message;
}

/// Only works after wrap_jpeg_setup_error_handler has been called
void * wrap_jpeg_get_custom_state(j_decompress_ptr codec_info)
{
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    return state->custom_state;
}

void wrap_jpeg_set_downscale_type(j_decompress_ptr codec_info, bool scale_luma_spatially, bool gamma_correct_for_srgb_during_spatial_luma_scaling){
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;

    state->scale_luma_spatially = scale_luma_spatially;
    state->gamma_correct_for_srgb_during_spatial_luma_scaling = gamma_correct_for_srgb_during_spatial_luma_scaling;
}

bool wrap_jpeg_create_decompress(j_decompress_ptr codec_info){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;

    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    jpeg_create_decompress(codec_info);
    return true;
}


static void wrap_jpeg_source_init(j_decompress_ptr codec_info){
    struct wrap_jpeg_source_manager *mgr = (struct wrap_jpeg_source_manager *) codec_info->src;

    if (mgr != NULL && mgr->init_source_fn != NULL){
        if (!mgr->init_source_fn(codec_info, mgr->custom_state)){
            struct jpeg_error_mgr * err = codec_info->err;
            err->msg_code = JERR_INPUT_EMPTY;
            err->error_exit((j_common_ptr)codec_info);
        }
    }
}
static void wrap_jpeg_source_term(j_decompress_ptr codec_info){
    struct wrap_jpeg_source_manager *mgr = (struct wrap_jpeg_source_manager *) codec_info->src;

    if (mgr != NULL && mgr->init_source_fn != NULL){
        if (!mgr->term_source_fn(codec_info, mgr->custom_state)){
            struct jpeg_error_mgr * err = codec_info->err;
            err->msg_code = JERR_INPUT_EMPTY;
            err->error_exit((j_common_ptr)codec_info);
        }
    }
}
static boolean wrap_jpeg_source_fill_input_buffer(j_decompress_ptr codec_info){
    struct wrap_jpeg_source_manager *mgr = (struct wrap_jpeg_source_manager *) codec_info->src;

    if (mgr != NULL && mgr->fill_input_buffer_fn != NULL){
        bool suspend = TRUE;
        if (!mgr->fill_input_buffer_fn(codec_info, mgr->custom_state, &suspend)){
            struct jpeg_error_mgr * err = codec_info->err;
            err->msg_code = JERR_INPUT_EMPTY;
            err->error_exit((j_common_ptr)codec_info);
        }
        return suspend;
    }
    return TRUE;
}
static void wrap_jpeg_source_skip_bytes(j_decompress_ptr codec_info, long byte_count){
    struct wrap_jpeg_source_manager *mgr = (struct wrap_jpeg_source_manager *) codec_info->src;

    if (mgr != NULL && mgr->skip_input_data_fn != NULL){
        if (!mgr->skip_input_data_fn(codec_info, mgr->custom_state, byte_count)){
            struct jpeg_error_mgr * err = codec_info->err;
            err->msg_code = JERR_INPUT_EMPTY;
            err->error_exit((j_common_ptr)codec_info);
        }
    }
}

void wrap_jpeg_setup_source_manager(struct wrap_jpeg_source_manager * manager){
    manager->shared_mgr.init_source = wrap_jpeg_source_init;
    manager->shared_mgr.term_source = wrap_jpeg_source_term;
    manager->shared_mgr.fill_input_buffer = wrap_jpeg_source_fill_input_buffer;
    manager->shared_mgr.skip_input_data = wrap_jpeg_source_skip_bytes;
    manager->shared_mgr.resync_to_restart = jpeg_resync_to_restart;
    manager->shared_mgr.bytes_in_buffer = 0;
    manager->shared_mgr.next_input_byte = NULL;

}



bool wrap_jpeg_save_markers(j_decompress_ptr codec_info,
                                int marker_code,
                                unsigned int length_limit){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    jpeg_save_markers(codec_info, marker_code, length_limit);
    return true;
}

bool wrap_jpeg_read_header(j_decompress_ptr codec_info){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    if (!jpeg_read_header(codec_info, TRUE)){
        struct jpeg_error_mgr * err = codec_info->err;
        err->msg_code = JERR_CANT_SUSPEND;
        err->error_exit((j_common_ptr)codec_info);
    }
    return true;
}

bool wrap_jpeg_start_decompress(j_decompress_ptr codec_info){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    if (!jpeg_start_decompress(codec_info)){
        struct jpeg_error_mgr * err = codec_info->err;
        err->msg_code = JERR_CANT_SUSPEND;
        err->error_exit((j_common_ptr)codec_info);
    }
    return true;
}


bool wrap_jpeg_read_scan_lines(j_decompress_ptr codec_info, uint8_t ** scan_lines, uint32_t max_scan_lines, uint32_t * scan_lines_read){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    *scan_lines_read = jpeg_read_scanlines(codec_info,  scan_lines, max_scan_lines);
    return true;
}

bool wrap_jpeg_finish_decompress(j_decompress_ptr codec_info){
    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;
    if (setjmp(state->error_handler_jmp)) {
        return false;
    }

    if (!jpeg_finish_decompress(codec_info)){
        struct jpeg_error_mgr * err = codec_info->err;
        err->msg_code = JERR_CANT_SUSPEND;
        err->error_exit((j_common_ptr)codec_info);
    }
    return true;
}




void jpeg_idct_spatial_srgb_1x1(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_2x2(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_3x3(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_4x4(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_5x5(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_6x6(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_srgb_7x7(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_1x1(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_2x2(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_3x3(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_4x4(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_5x5(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_6x6(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

void jpeg_idct_spatial_7x7(j_decompress_ptr codec_info, jpeg_component_info * compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col);

static void wrap_jpeg_idct_method_selector(j_decompress_ptr codec_info, jpeg_component_info * compptr,
                                           jpeg_idct_method * set_idct_method, int * set_idct_category)
{
    if (compptr->component_id != 1)
        return;
#if JPEG_LIB_VERSION >= 70
    int scaled = compptr->DCT_h_scaled_size;
#else
    int scaled = compptr->DCT_scaled_size;
#endif

    /* codec_info->err really points to a wrap_jpeg_error_state struct, so coerce pointer */
    struct wrap_jpeg_error_state *state = (struct wrap_jpeg_error_state *) codec_info->err;

    if (scaled > 0 && scaled < 8 && state->scale_luma_spatially) {
        if (state->gamma_correct_for_srgb_during_spatial_luma_scaling) {
            switch (scaled) {
                case 1:
                    *set_idct_method = jpeg_idct_spatial_srgb_1x1;
                    break;
                case 2:
                    *set_idct_method = jpeg_idct_spatial_srgb_2x2;
                    break;
                case 3:
                    *set_idct_method = jpeg_idct_spatial_srgb_3x3;
                    break;
                case 4:
                    *set_idct_method = jpeg_idct_spatial_srgb_4x4;
                    break;
                case 5:
                    *set_idct_method = jpeg_idct_spatial_srgb_5x5;
                    break;
                case 6:
                    *set_idct_method = jpeg_idct_spatial_srgb_6x6;
                    break;
                case 7:
                    *set_idct_method = jpeg_idct_spatial_srgb_7x7;
                    break;
            }
        } else {
            switch (scaled) {
                case 1:
                    *set_idct_method = jpeg_idct_spatial_1x1;
                    break;
                case 2:
                    *set_idct_method = jpeg_idct_spatial_2x2;
                    break;
                case 3:
                    *set_idct_method = jpeg_idct_spatial_3x3;
                    break;
                case 4:
                    *set_idct_method = jpeg_idct_spatial_4x4;
                    break;
                case 5:
                    *set_idct_method = jpeg_idct_spatial_5x5;
                    break;
                case 6:
                    *set_idct_method = jpeg_idct_spatial_6x6;
                    break;
                case 7:
                    *set_idct_method = jpeg_idct_spatial_7x7;
                    break;
            }
        }
        *set_idct_category = JDCT_ISLOW;
        // populate_lookup_tables(state);
    }
}


void wrap_jpeg_set_idct_method_selector(j_decompress_ptr codec_info){
    jpeg_set_idct_method_selector(codec_info, wrap_jpeg_idct_method_selector);
}


//
//
//static bool flow_codecs_jpg_decoder_BeginRead(flow_c * c, struct flow_codecs_jpeg_decoder_state * state)
//{
//    if (state->stage != flow_codecs_jpg_decoder_stage_NotStarted) {
//        FLOW_error(c, flow_status_Invalid_internal_state);
//        return false;
//    }
//    if (!flow_codecs_jpg_decoder_reset(c, state)) {
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        FLOW_error_return(c);
//    }
//    state->stage = flow_codecs_jpg_decoder_stage_BeginRead;
//
//    state->codec_info = (struct jpeg_decompress_struct *)FLOW_calloc(c, 1, sizeof(struct jpeg_decompress_struct));
//    if (state->codec_info == NULL) {
//        FLOW_error(c, flow_status_Out_of_memory);
//        flow_codecs_jpg_decoder_reset(c, state);
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        return false;
//    }
//
//    /* We set up the normal JPEG error routines, then override error_exit and output_message. */
//    state->codec_info->err = jpeg_std_error(&state->error_mgr);
//    state->error_mgr.error_exit = jpeg_error_exit;
//    state->error_mgr.output_message = flow_jpeg_output_message; // Prevent USE_WINDOWS_MESSAGEBOX
//
//
//    /* Establish the setjmp return context for jpeg_error_exit to use. */
//    if (setjmp(state->error_handler_jmp)) {
//        /* If we get here, the JPEG code has signaled an error.
//         */
//        if (state->stage != flow_codecs_jpg_decoder_stage_Failed) {
//            exit(404); // This should never happen, jpeg_error_exit should fix it.
//        }
//        return false;
//    }
//    /* Now we can initialize the JPEG decompression object. */
//    jpeg_create_decompress(state->codec_info);
//
//    // Set a source manager for reading from memory
//    flow_codecs_jpeg_setup_source_manager(state->codec_info, state->io);
//
//    /* Step 3: read file parameters with jpeg_read_header() */
//
//    /* Tell the library to keep any APP2 data it may find */
//    jpeg_save_markers(state->codec_info, ICC_MARKER, 0xFFFF);
//    jpeg_save_markers(state->codec_info, EXIF_JPEG_MARKER, 0xffff);
//
//    (void)jpeg_read_header(state->codec_info, TRUE);
//
//    if (!flow_codecs_jpg_decoder_interpret_metadata(c, state)) {
//        flow_codecs_jpg_decoder_reset(c, state);
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        FLOW_error_return(c);
//    }
//    /* We can ignore the return value from jpeg_read_header since
//     *   (a) suspension is not possible with the stdio data source, and
//     *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
//     * See libjpeg.txt for more info.
//     */
//
//    /* Step 4: set parameters for decompression */
//    state->codec_info->out_color_space = JCS_EXT_BGRA;
//
//    state->w = state->codec_info->image_width;
//    state->h = state->codec_info->image_height;
//    return true;
//}
//
//static bool flow_codecs_jpg_decoder_FinishRead(flow_c * c, struct flow_codecs_jpeg_decoder_state * state)
//{
//    if (state->stage != flow_codecs_jpg_decoder_stage_BeginRead) {
//        FLOW_error(c, flow_status_Invalid_internal_state);
//        return false;
//    }
//    // We let the caller create the buffer
//    //    state->pixel_buffer =  (jpg_bytep)FLOW_calloc (c, state->pixel_buffer_size, sizeof(jpg_bytep));
//    if (state->pixel_buffer == NULL || state->canvas == NULL) {
//        flow_codecs_jpg_decoder_reset(c, state);
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        FLOW_error(c, flow_status_Out_of_memory);
//        return false;
//    }
//    if (setjmp(state->error_handler_jmp)) {
//        // Execution comes back to this point if an error happens
//        return false;
//    }
//    /* Step 5: Start decompressor */
//
//    (void)jpeg_start_decompress(state->codec_info);
//
//    /* We may need to do some setup of our own at this point before reading
// * the data.  After jpeg_start_decompress() we have the correct scaled
// * output image dimensions available, as well as the output colormap
// * if we asked for color quantization.
// * In this example, we need to make an output work buffer of the right size.
// */
//    /* JSAMPLEs per row in output buffer */
//
//    // state->row_stride = state->codec_info->output_width * state->codec_info->output_components;
//    state->channels = state->codec_info->output_components;
//    state->color.gamma = state->codec_info->output_gamma;
//
//    state->stage = flow_codecs_jpg_decoder_stage_FinishRead;
//
//
//    state->pixel_buffer_row_pointers = flow_bitmap_create_row_pointers(c, state->pixel_buffer, state->pixel_buffer_size,
//                                                                       state->row_stride, state->h);
//    if (state->pixel_buffer_row_pointers == NULL) {
//        flow_codecs_jpg_decoder_reset(c, state);
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        FLOW_error_return(c);
//    }
//
//    uint32_t scanlines_read = 0;
//    /* Step 6: while (scan lines remain to be read) */
//    /*           jpeg_read_scanlines(...); */
//
//    /* Here we use the library's state variable codec_info.output_scanline as the
//     * loop counter, so that we don't have to keep track ourselves.
//     */
//    while (state->codec_info->output_scanline < state->codec_info->output_height) {
//        /* jpeg_read_scanlines expects an array of pointers to scanlines.
//         * Here the array is only one element long, but you could ask for
//         * more than one scanline at a time if that's more convenient.
//         */
//        scanlines_read = jpeg_read_scanlines(
//            state->codec_info, &state->pixel_buffer_row_pointers[state->codec_info->output_scanline], (JDIMENSION)state->h);
//    }
//
//    if (scanlines_read < 1) {
//        return false;
//    }
//
//    // We must read the markers before jpeg_finish_decompress destroys them
//
//    if (!flow_codecs_jpg_decoder_interpret_metadata(c, state)) {
//        flow_codecs_jpg_decoder_reset(c, state);
//        state->stage = flow_codecs_jpg_decoder_stage_Failed;
//        FLOW_error_return(c);
//    }
//
//    /* Step 7: Finish decompression */
//
//    (void)jpeg_finish_decompress(state->codec_info);
//
//    /* We can ignore the return value since suspension is not possible
//     * with the stdio data source.
//     */
//
//    jpeg_destroy_decompress(state->codec_info);
//    FLOW_free(c, state->codec_info);
//    state->codec_info = NULL;
//
//    return true;
//}
