BITS 32
ORG 0

; Feature 17 reads across ISO sectors and checks this repeating byte lane.
times 4096 db $
