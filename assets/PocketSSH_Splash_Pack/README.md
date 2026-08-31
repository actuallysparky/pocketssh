# Pocket SSH splash screens

Native-resolution splash artwork for LILYGO's T-LoRa Pager and T-Deck.

## Included assets

| Platform | Static fallback | Animated splash |
|---|---|---|
| T-LoRa Pager, landscape | `assets/t-pager/pocket_ssh_t-pager_480x222.png` | `assets/t-pager/pocket_ssh_t-pager_480x222.gif` |
| T-Deck | `assets/t-deck/pocket_ssh_t-deck_320x240.png` | `assets/t-deck/pocket_ssh_t-deck_320x240.gif` |

The pack also includes the full-resolution transparent logo mark under `source/`.

## Animation

Each GIF contains 16 native-resolution frames at 100 ms per frame. The 1.6-second loop uses a cursor blink, subtle brightness breathing, and a single phosphor scan-line sweep. The indexed eight-color palette keeps decoding and storage requirements modest.

## Palette

- Background: `#000D05`
- Phosphor green: `#33FF66`
- Hot phosphor: `#B6FFBC`
- Dim phosphor shades: `#074C1F`, `#0C8B3B`

If the target firmware does not include a GIF decoder, use the corresponding PNG without changing the layout.
