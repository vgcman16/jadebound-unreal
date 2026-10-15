// Included once by JadeboundProgressionActor.cpp at global scope. Deliberately
// shares that translation unit's unchanged private ReadJournal envelope helper;
// this is not a separate CPP and does not rely on a unity build.
bool AJadeboundProgressionActor::ReadSave(bool Initial)
{
    using namespace Jadebound::Persistence;
    if (!Core || !HasAuthority() || GetNetMode() == NM_Client || bPersistenceEndingPlay ||
        !GetWorld() || GetWorld()->bIsTearingDown || IsActorBeingDestroyed() ||
        bUnsavedCandidate || HeldSaveCandidate || bPersistenceBlocked ||
        PersistencePresentation.FailureKind != EJadeboundPersistenceFailureKind::None) return false;
    if (!HasJournalWriterLease()) return false;
    bV3JournalAdmitted = false;
    auto Refuse = [this](const TCHAR* Message)
    {
        SetPersistenceFailure(EJadeboundPersistenceFailureKind::RecoveryRequired,FText::FromString(Message));
        return false;
    };
    JournalNamespacePaths Paths;
    if (!BuildJournalNamespacePaths(bDevelopmentModel,GetSaveCharacterId(),Paths))
        return Refuse(TEXT("Your character save needs recovery. Existing files are protected."));
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    // As in guarded V2, canonical next-format presence is sufficient to refuse.
    // No codec or assumptions about the unknown future contents are needed.
    if (HasForwardV4Entry(Files,Paths))
        return Refuse(TEXT("This character has a newer save format or a conflicting save entry. Use the matching build; existing files are protected."));
    const JournalPresence Presence = ObserveJournalPresence(Files,Paths);
    const JournalFormat Format = SelectJournalFormat(Presence);
    if (Format == JournalFormat::None)
    {
        if (!Initial) return Refuse(TEXT("Your character save needs recovery. Existing files are protected."));
        LastSaveSlot = 1; // A new V3 pair starts at A; no old bank is a destination.
        bV3JournalAdmitted = true;
        RefreshState();
        return WriteSave();
    }
    const int32 NamespaceVersion = static_cast<int32>(Format);
    const int32 NamespaceIndex = NamespaceVersion-1;
    UJadeboundCharacterSave* Best = nullptr;
    Jadebound::Snapshot BestV3;
    JadeboundLegacyV2::Snapshot BestV2;
    JadeboundLegacyV1::Snapshot BestV1;
    uint64 BestRevision = 0;
    TArray<uint8> BestBytes;
    int32 BestSlot = 1;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const FString& Path = Paths.Banks[NamespaceIndex][Slot];
        if (!Files.FileExists(*Path)) continue; // Presence was already retained.
        TArray<uint8> OriginalBytes;
        auto* Save = ReadJournal(Path,&OriginalBytes);
        if (!Save || Save->CharacterId != GetSaveCharacterId()) continue;
        if (Save->State.Version > NamespaceVersion)
            return Refuse(TEXT("Your character save is incompatible with this bank format. Use the matching build; existing files are protected."));
        Jadebound::Snapshot V3;
        JadeboundLegacyV2::Snapshot V2;
        JadeboundLegacyV1::Snapshot V1;
        uint64 Revision = 0;
        switch (Format)
        {
        case JournalFormat::V3:
            if (!DecodeStateV3(Save->State,V3) || !ValidateDropsV3(V3,Save->PendingDrops)) continue;
            Revision = V3.Revision; break;
        case JournalFormat::V2:
            if (!DecodeStateV2(Save->State,V2) || !ValidateDropsV2(V2,Save->PendingDrops)) continue;
            Revision = V2.Revision; break;
        case JournalFormat::V1:
            // Exact original uint64 ranking precedes new signed eligibility.
            if (!DecodeStateV1(Save->State,V1) || !ValidateDropsV1(V1,Save->PendingDrops)) continue;
            Revision = V1.Revision; break;
        default: return Refuse(TEXT("Your character save format needs recovery. Existing files are protected."));
        }
        if (Best && Revision == BestRevision && BestBytes != OriginalBytes)
            return Refuse(TEXT("Your character save has conflicting records. Existing files are protected."));
        if (!Best || Revision > BestRevision)
        {
            Best = Save; BestRevision = Revision; BestBytes = MoveTemp(OriginalBytes); BestSlot = Slot;
            if (Format == JournalFormat::V3) BestV3 = std::move(V3);
            else if (Format == JournalFormat::V2) BestV2 = std::move(V2);
            else BestV1 = std::move(V1);
        }
    }
    // Even an empty/malformed/directory-only selected pair blocks older pairs.
    if (!Best) return Refuse(TEXT("Your character save needs recovery. Existing files are protected."));
    if (Format != JournalFormat::V3)
    {
        bReady = false;
        bServerModelInitialized = false;
        Jadebound::Snapshot Migration;
        const bool Built = Format == JournalFormat::V1
            ? Jadebound::Source145BuildLegacyV1TorsoMigrationCandidate(BestV1,Migration)
            : Jadebound::BuildTorsoMigrationCandidate(BestV2,Migration);
        if (!Built || !ValidateDropsV3(Migration,Best->PendingDrops) || !Core->LoadSnapshot(Migration).Succeeded())
            return Refuse(TEXT("Your character save cannot be migrated safely. Existing files are protected."));
        // One representation revision, no defeat/pickup/reward; old pairs survive.
        PendingDrops = Best->PendingDrops;
        LastCommittedSave = Best; // ORIGINAL remains the witness until V3 readback.
        LastSaveSlot = 1;
        bV3JournalAdmitted = true;
        RefreshState();
        const bool Saved = WriteSave(); // Existing held object owns this exact candidate.
        RefreshDropVisuals();
        return Saved;
    }
    if (!Core->LoadSnapshot(BestV3).Succeeded())
        return Refuse(TEXT("Your character save needs recovery. Existing files are protected."));
    PendingDrops = Best->PendingDrops;
    LastCommittedSave = Best;
    LastSaveSlot = BestSlot;
    bV3JournalAdmitted = true;
    ClearPersistenceFailure();
    RefreshState();
    RefreshDropVisuals();
    return true;
}
