use super::*;

#[test]
fn direct_view_borrows_validated_input_spans_without_truncation() {
    let source = LocalIdentity::from_private_key(&[0x11; 64]);
    let recipient = LocalIdentity::from_private_key(&[0x22; 64]);
    let mut tx = core::ptr::null_mut();
    let mut rx = core::ptr::null_mut();
    unsafe {
        assert_eq!(rs_handheld_rns_init(&mut tx), RsHandheldStatus::Ok);
        assert_eq!(rs_handheld_rns_init(&mut rx), RsHandheldStatus::Ok);
        assert_eq!(
            rs_handheld_rns_load_identity(tx, &[0x11; 64]),
            RsHandheldStatus::Ok
        );
        assert_eq!(
            rs_handheld_rns_load_identity(rx, &[0x22; 64]),
            RsHandheldStatus::Ok
        );
    }
    for title_len in [0, 1, 1024, 1500, 2999, 3000] {
        let title: Vec<_> = (0..title_len).map(|i| (i % 256) as u8).collect();
        let content: Vec<_> = (0..3000 - title_len)
            .map(|i| (255 - i % 256) as u8)
            .collect();
        let mut packed = vec![0; 4096];
        let mut length = 0;
        let mut dest = [0; 16];
        let mut mid = [0; 32];
        unsafe {
            assert_eq!(
                rs_handheld_rns_lxmf_build_link(
                    tx,
                    recipient.public_key(),
                    1234567890.5,
                    title.as_ptr(),
                    title.len(),
                    content.as_ptr(),
                    content.len(),
                    packed.as_mut_ptr(),
                    packed.len(),
                    &mut length,
                    &mut dest,
                    &mut mid,
                ),
                RsHandheldStatus::Ok
            );
        }
        packed.truncate(length);
        let original = packed.clone();
        let mut view = RsHandheldLxmfView::default();
        unsafe {
            assert_eq!(
                rs_handheld_rns_lxmf_parse_link_view(
                    rx,
                    packed.as_ptr(),
                    packed.len(),
                    source.public_key(),
                    &mut view,
                ),
                RsHandheldStatus::Ok
            );
        }
        let span = |offset: u32, length: u32| &packed[offset as usize..(offset + length) as usize];
        assert_eq!(span(view.title_offset, view.title_len), title);
        assert_eq!(span(view.content_offset, view.content_len), content);
        assert_eq!(view.message_id, mid);
        assert_eq!(view.source_hash, source.lxmf_delivery_hash());
        assert_eq!(view.timestamp, 1234567890.5);
        assert_eq!(view.is_reaction, 0);
        assert_eq!(packed, original);
        // No valid spans are returned for the wrong source key or a forgery.
        unsafe {
            assert_eq!(
                rs_handheld_rns_lxmf_parse_link_view(
                    rx,
                    packed.as_ptr(),
                    packed.len(),
                    recipient.public_key(),
                    &mut view,
                ),
                RsHandheldStatus::ErrCrypto
            );
            packed[40] ^= 1;
            assert_eq!(
                rs_handheld_rns_lxmf_parse_link_view(
                    rx,
                    packed.as_ptr(),
                    packed.len(),
                    source.public_key(),
                    &mut view,
                ),
                RsHandheldStatus::ErrCrypto
            );
        }
    }
    unsafe {
        assert_eq!(
            rs_handheld_rns_lxmf_parse_link_view(
                rx,
                core::ptr::null(),
                0,
                source.public_key(),
                core::ptr::null_mut(),
            ),
            RsHandheldStatus::ErrInvalidArg
        );
        rs_handheld_rns_shutdown(tx);
        rs_handheld_rns_shutdown(rx);
    }
}
